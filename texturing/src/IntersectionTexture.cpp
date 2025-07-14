#include "vc/texturing/IntersectionTexture.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include "vc/core/util/Slicing.hpp"

using namespace volcart;
using namespace volcart::texturing;

using Texture = IntersectionTexture::Texture;

auto IntersectionTexture::New() -> Pointer
{
    return std::make_shared<IntersectionTexture>();
}

IntersectionTexture::~IntersectionTexture()
{
    if (chunkCache_) {
        delete static_cast<ChunkCache*>(chunkCache_);
        chunkCache_ = nullptr;
    }
}

auto IntersectionTexture::compute() -> Texture
{
    // Setup
    result_.clear();
    auto height = static_cast<int>(ppm_->height());
    auto width = static_cast<int>(ppm_->width());

    // Output image
    cv::Mat image = cv::Mat::zeros(height, width, CV_16UC1);

    // Get the mappings
    auto mappings = ppm_->getMappingCoords();

    // Sort the mappings by Z-value
    std::sort(
        mappings.begin(), mappings.end(),
        [&](const auto& lhs, const auto& rhs) {
            return (*ppm_)(lhs.y, lhs.x)[2] < (*ppm_)(rhs.y, rhs.x)[2];
        });

    // Prepare for batch processing
    cv::Mat_<cv::Vec3f> coordinates(static_cast<int>(mappings.size()), 1);
    std::vector<cv::Point2i> pixelCoords;
    pixelCoords.reserve(mappings.size());
    
    // Collect all coordinates
    int idx = 0;
    for (const auto [y, x] : mappings) {
        const auto& m = ppm_->getMapping(y, x);
        coordinates(idx, 0) = cv::Vec3f(static_cast<float>(m[0]), 
                                        static_cast<float>(m[1]), 
                                        static_cast<float>(m[2]));
        pixelCoords.push_back({static_cast<int>(x), static_cast<int>(y)});
        idx++;
    }
    
    // Create cache if volume is zarr
    if (vol_->isZarr && !chunkCache_) {
        chunkCache_ = new ChunkCache(cacheSize_);
    }
    
    // Batch interpolate
    cv::Mat intensities = vol_->batchInterpolateAt(coordinates, chunkCache_);
    
    // Assign results to output image
    progressStarted();
    for (size_t i = 0; i < pixelCoords.size(); i++) {
        progressUpdated(i);
        const auto& pt = pixelCoords[i];
        image.at<uint16_t>(pt.y, pt.x) = intensities.at<uint16_t>(static_cast<int>(i), 0);
    }
    progressComplete();

    // Set output
    result_.push_back(image);

    return result_;
}
