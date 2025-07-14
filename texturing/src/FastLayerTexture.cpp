#include "vc/texturing/FastLayerTexture.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <fstream>

#include <opencv2/imgproc.hpp>
#include <nlohmann/json.hpp>

#include "vc/core/util/Logging.hpp"
#include "vc/core/util/Slicing.hpp"
#include "vc/core/neighborhood/LineGenerator.hpp"
#include "z5/factory.hxx"
#include "z5/dataset.hxx"

using namespace volcart;
using namespace volcart::texturing;

auto FastLayerTexture::New() -> Pointer
{
    return std::make_shared<FastLayerTexture>();
}

void FastLayerTexture::cachePPMData()
{
    auto height = static_cast<int>(ppm_->height());
    auto width = static_cast<int>(ppm_->width());
    
    // Initialize cached matrices
    cachedPositions_ = cv::Mat_<cv::Vec3d>(height, width);
    cachedNormals_ = cv::Mat_<cv::Vec3d>(height, width);
    cachedValidMask_ = cv::Mat_<uint8_t>(height, width, static_cast<uint8_t>(0));
    
    // Cache PPM data
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (ppm_->hasMapping(y, x)) {
                const auto& m = ppm_->getMapping(y, x);
                cachedPositions_(y, x) = cv::Vec3d(m[0], m[1], m[2]);
                cachedNormals_(y, x) = cv::Vec3d(m[3], m[4], m[5]);
                cachedValidMask_(y, x) = 255;
            }
        }
    }
    
    Logger()->debug("FastLayerTexture: Cached PPM data for {}x{} pixels", width, height);
}

auto FastLayerTexture::compute() -> Texture
{
    // Validate inputs
    if (!ppm_) {
        throw std::runtime_error("FastLayerTexture: No PPM set");
    }
    if (!vol_) {
        throw std::runtime_error("FastLayerTexture: No volume set");
    }
    if (!gen_) {
        throw std::runtime_error("FastLayerTexture: No generator set");
    }
    
    // Check that generator is a LineGenerator
    auto lineGen = std::dynamic_pointer_cast<LineGenerator>(gen_);
    if (!lineGen) {
        throw std::runtime_error("FastLayerTexture: Generator must be a LineGenerator");
    }
    
    // Get generator settings
    auto interval = gen_->samplingInterval();
    auto direction = gen_->samplingDirection();
    
    // Calculate number of layers
    std::size_t numLayers = gen_->extents()[0];
    
    Logger()->info("FastLayerTexture: Rendering {} layers with interval {}", numLayers, interval);
    
    // Get output size from PPM
    auto height = static_cast<int>(ppm_->height());
    auto width = static_cast<int>(ppm_->width());
    cv::Size outputSize(width, height);
    
    // Cache PPM data for faster access
    cachePPMData();
    
    // Setup output images
    result_.clear();
    result_.reserve(numLayers);
    for (std::size_t i = 0; i < numLayers; i++) {
        result_.emplace_back(cv::Mat::zeros(height, width, CV_8UC1));
    }
    
    // Direct zarr access for performance
    if (!vol_->isZarr) {
        throw std::runtime_error("FastLayerTexture: Volume must be a zarr volume");
    }
    
    // Get the zarr dataset - use scale 0 for now (full resolution)
    int scale = 0;  // TODO: Make this configurable
    auto zarrPath = vol_->path();
    
    z5::filesystem::handle::Group group(zarrPath, z5::FileMode::FileMode::r);
    
    // Read the .zarray file to get the dimension separator
    nlohmann::json zarray_json;
    std::ifstream zarray_file(zarrPath / std::to_string(scale) / ".zarray");
    if (!zarray_file.is_open()) {
        throw std::runtime_error("FastLayerTexture: Failed to open .zarray file");
    }
    zarray_file >> zarray_json;
    std::string dim_separator = zarray_json.value("dimension_separator", ".");
    
    z5::filesystem::handle::Dataset ds_handle(group, std::to_string(scale), dim_separator);
    std::unique_ptr<z5::Dataset> dataset = z5::filesystem::openDataset(ds_handle);
    
    // Get zarr chunk shape for optimal processing
    auto zarrChunkShape = dataset->chunking().blockShape();
    Logger()->debug("FastLayerTexture: Zarr chunk shape is {}x{}x{}", 
                    zarrChunkShape[0], zarrChunkShape[1], zarrChunkShape[2]);
    
    // Align processing chunk size with zarr chunks if possible
    // For 2D access patterns, we care about the X and Y dimensions
    if (zarrChunkShape.size() >= 3) {
        // Try to use a multiple of zarr chunk dimensions for better alignment
        int zarrChunkY = static_cast<int>(zarrChunkShape[1]);
        int zarrChunkX = static_cast<int>(zarrChunkShape[2]);
        
        // Use zarr chunk size or a multiple that's close to our target chunk size
        if (zarrChunkX > 0 && zarrChunkY > 0) {
            int multipleX = std::max(1, chunkSize_ / zarrChunkX);
            int multipleY = std::max(1, chunkSize_ / zarrChunkY);
            chunkSize_ = std::max(zarrChunkX * multipleX, zarrChunkY * multipleY);
            Logger()->debug("FastLayerTexture: Adjusted chunk size to {} for better zarr alignment", chunkSize_);
        }
    }
    
    // Calculate scale factor for coordinates
    float ds_scale = 1.0f;
    if (scale > 0) {
        ds_scale = std::pow(2.0f, -static_cast<float>(scale));
    }
    
    Logger()->debug("FastLayerTexture: Using zarr scale {} with coordinate scale factor {}", scale, ds_scale);
    
    // Create chunk cache
    ChunkCache chunkCache(cacheSize_);
    
    // Process in chunks
    progressStarted.send();
    int totalChunks = ((width + chunkSize_ - 1) / chunkSize_) * 
                      ((height + chunkSize_ - 1) / chunkSize_);
    int processedChunks = 0;
    
    for (int y = 0; y < height; y += chunkSize_) {
        for (int x = 0; x < width; x += chunkSize_) {
            int w = std::min(chunkSize_, width - x);
            int h = std::min(chunkSize_, height - y);
            cv::Rect chunk(x, y, w, h);
            
            processChunk(dataset.get(), &chunkCache, chunk, outputSize, result_, ds_scale);
            
            processedChunks++;
            progressUpdated.send(static_cast<std::size_t>(processedChunks));
        }
    }
    
    progressComplete.send();
    
    return result_;
}

void FastLayerTexture::processChunk(
    z5::Dataset* dataset,
    ChunkCache* cache,
    const cv::Rect& chunk,
    const cv::Size& outputSize,
    std::vector<cv::Mat>& outputs,
    float scaleCoordinates)
{
    // Get generator settings
    auto interval = gen_->samplingInterval();
    auto direction = gen_->samplingDirection();
    std::size_t numLayers = gen_->extents()[0];
    
    // Extract chunk of positions and normals
    cv::Mat_<cv::Vec3f> chunkPositions(chunk.height, chunk.width);
    cv::Mat_<cv::Vec3f> chunkNormals(chunk.height, chunk.width);
    
    // Copy valid pixels to chunk matrices
    for (int y = 0; y < chunk.height; ++y) {
        for (int x = 0; x < chunk.width; ++x) {
            int srcY = chunk.y + y;
            int srcX = chunk.x + x;
            
            if (cachedValidMask_(srcY, srcX)) {
                const cv::Vec3d& pos = cachedPositions_(srcY, srcX);
                const cv::Vec3d& normal = cachedNormals_(srcY, srcX);
                chunkPositions(y, x) = cv::Vec3f(
                    static_cast<float>(pos[0]),
                    static_cast<float>(pos[1]),
                    static_cast<float>(pos[2])
                );
                chunkNormals(y, x) = cv::Vec3f(
                    static_cast<float>(normal[0]),
                    static_cast<float>(normal[1]),
                    static_cast<float>(normal[2])
                );
            } else {
                chunkPositions(y, x) = cv::Vec3f(NAN, NAN, NAN);
                chunkNormals(y, x) = cv::Vec3f(NAN, NAN, NAN);
            }
        }
    }
    
    // Process each layer with a single readInterpolated3D call per layer
    for (std::size_t layer = 0; layer < numLayers; ++layer) {
        // Calculate offset for this layer
        double offset = (static_cast<double>(layer) - numLayers / 2.0) * interval;
        
        // Apply direction constraint
        if (direction == Direction::Positive && offset < 0) continue;
        if (direction == Direction::Negative && offset > 0) continue;
        
        // Create offset positions for entire chunk
        cv::Mat_<cv::Vec3f> offsetPositions = chunkPositions + 
            static_cast<float>(offset * scaleCoordinates) * chunkNormals;
        
        // Apply coordinate scaling
        if (scaleCoordinates != 1.0f) {
            offsetPositions *= scaleCoordinates;
        }
        
        // Read interpolated values for entire chunk
        cv::Mat_<uint8_t> intensities;
        readInterpolated3D(intensities, dataset, offsetPositions, cache);
        
        // Copy intensities to output
        for (int y = 0; y < chunk.height; ++y) {
            for (int x = 0; x < chunk.width; ++x) {
                if (cachedValidMask_(chunk.y + y, chunk.x + x)) {
                    outputs[layer].at<uint8_t>(chunk.y + y, chunk.x + x) = 
                        intensities(y, x);
                }
            }
        }
    }
}

std::size_t FastLayerTexture::progressIterations() const
{
    if (!ppm_) {
        return 0;
    }
    
    // Get PPM dimensions
    auto height = static_cast<int>(ppm_->height());
    auto width = static_cast<int>(ppm_->width());
    
    // Calculate total number of chunks
    return ((width + chunkSize_ - 1) / chunkSize_) * 
           ((height + chunkSize_ - 1) / chunkSize_);
}
