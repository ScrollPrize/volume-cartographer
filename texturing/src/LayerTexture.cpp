#include "vc/texturing/LayerTexture.hpp"

#include <cstddef>
#include <tuple>
#include <vector>

#include <opencv2/core.hpp>

#include "vc/core/util/Iteration.hpp"

using namespace volcart;
using namespace volcart::texturing;

using Texture = LayerTexture::Texture;

auto LayerTexture::New() -> Pointer { return std::make_shared<LayerTexture>(); }

auto LayerTexture::compute() -> Texture
{
    // Setup
    result_.clear();
    auto height = static_cast<int>(ppm_->height());
    auto width = static_cast<int>(ppm_->width());

    // Create cache if volume is zarr
    if (vol_->isZarr && !chunkCache_) {
        chunkCache_ = std::make_unique<ChunkCache>(cacheSize_);
    }

    // Get generator settings
    auto radius = gen_->samplingRadius();
    auto interval = gen_->samplingInterval();
    auto direction = gen_->samplingDirection();
    
    // Calculate number of layers
    std::size_t numLayers = gen_->extents()[0];
    
    // Setup output images
    for (std::size_t i = 0; i < numLayers; i++) {
        result_.emplace_back(cv::Mat::zeros(height, width, CV_16UC1));
    }

    // Get the mappings
    auto mappings = ppm_->getMappingCoords();

    // Sort the mappings by Z-value
    std::sort(
        mappings.begin(), mappings.end(),
        [&](const auto& lhs, const auto& rhs) {
            return (*ppm_)(lhs.y, lhs.x)[2] < (*ppm_)(rhs.y, rhs.x)[2];
        });

    // Prepare coordinate collection
    std::vector<cv::Vec3f> allCoordinates;
    std::vector<std::tuple<std::size_t, int, int>> coordMapping; // layer, y, x
    
    // First pass: collect all coordinates
    for (const auto& coord : mappings) {
        const auto [y, x] = coord;
        const auto& m = ppm_->getMapping(y, x);
        const cv::Vec3d pos{m[0], m[1], m[2]};
        const cv::Vec3d normal{m[3], m[4], m[5]};
        
        // Generate positions along the line
        for (std::size_t layer = 0; layer < numLayers; layer++) {
            // Calculate offset for this layer
            double offset = (static_cast<double>(layer) - numLayers / 2.0) * interval;
            
            // Apply direction constraint
            if (direction == Direction::Positive && offset < 0) {
                continue;
            } else if (direction == Direction::Negative && offset > 0) {
                continue;
            }
            
            cv::Vec3d samplePos = pos + offset * normal;
            allCoordinates.push_back(cv::Vec3f(
                static_cast<float>(samplePos[0]), 
                static_cast<float>(samplePos[1]), 
                static_cast<float>(samplePos[2])
            ));
            coordMapping.push_back({layer, static_cast<int>(y), static_cast<int>(x)});
        }
    }
    
    // Batch interpolate all coordinates
    cv::Mat_<cv::Vec3f> coordMat(static_cast<int>(allCoordinates.size()), 1);
    for (size_t i = 0; i < allCoordinates.size(); i++) {
        coordMat(static_cast<int>(i), 0) = allCoordinates[i];
    }
    
    cv::Mat intensities = vol_->batchInterpolateAt(coordMat, chunkCache_.get());
    
    // Assign intensities to layer images
    progressStarted();
    for (size_t i = 0; i < coordMapping.size(); i++) {
        progressUpdated(i);
        auto [layer, y, x] = coordMapping[i];
        result_[layer].at<std::uint16_t>(y, x) = intensities.at<std::uint16_t>(static_cast<int>(i), 0);
    }
    progressComplete();

    return result_;
}
