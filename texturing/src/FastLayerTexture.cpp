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
    
    // Collect all 3D coordinates for this chunk
    std::vector<cv::Vec3f> coordinates;
    std::vector<std::tuple<int, int, std::size_t>> mapping; // x, y, layer
    
    for (int y = chunk.y; y < chunk.y + chunk.height; ++y) {
        for (int x = chunk.x; x < chunk.x + chunk.width; ++x) {
            // Check if this pixel has valid mapping
            if (ppm_->hasMapping(y, x)) {
                const auto& m = ppm_->getMapping(y, x);
                const cv::Vec3d pos{m[0], m[1], m[2]};
                const cv::Vec3d normal{m[3], m[4], m[5]};
                
                // Generate positions along the normal
                for (std::size_t layer = 0; layer < numLayers; ++layer) {
                    // Calculate offset for this layer
                    double offset = (static_cast<double>(layer) - numLayers / 2.0) * interval;
                    
                    // Apply direction constraint
                    if (direction == Direction::Positive && offset < 0) continue;
                    if (direction == Direction::Negative && offset > 0) continue;
                    
                    cv::Vec3d samplePos = pos + offset * normal;
                    
                    // Apply scale factor for zarr coordinates
                    samplePos *= scaleCoordinates;
                    
                    coordinates.push_back(cv::Vec3f(
                        static_cast<float>(samplePos[0]),
                        static_cast<float>(samplePos[1]),
                        static_cast<float>(samplePos[2])
                    ));
                    mapping.push_back({x, y, layer});
                }
            }
        }
    }
    
    // Batch interpolate all coordinates
    if (!coordinates.empty()) {
        cv::Mat_<cv::Vec3f> coordMat(static_cast<int>(coordinates.size()), 1);
        for (size_t i = 0; i < coordinates.size(); i++) {
            coordMat(static_cast<int>(i), 0) = coordinates[i];
        }
        
        cv::Mat_<uint8_t> intensities;
        readInterpolated3D(intensities, dataset, coordMat, cache);
        
        // Assign intensities to layer images
        for (size_t i = 0; i < mapping.size(); i++) {
            auto [x, y, layer] = mapping[i];
            // Store 8-bit value
            outputs[layer].at<std::uint8_t>(y, x) = intensities.at<std::uint8_t>(static_cast<int>(i), 0);
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
