#pragma once

/** @file */

#include <cstddef>
#include <memory>
#include <vector>

#include <opencv2/core.hpp>

#include "vc/core/types/PerPixelMap.hpp"
#include "vc/core/types/Volume.hpp"
#include "vc/core/types/Mixins.hpp"
#include "vc/core/neighborhood/NeighborhoodGenerator.hpp"

namespace z5 {
    class Dataset;
}

class ChunkCache;

namespace volcart::texturing
{

/**
 * @brief Fast layer texture generation for zarr volumes
 * 
 * This class provides a high-performance implementation for generating
 * layer textures from zarr volumes. It uses the PPM (PerPixelMap) for
 * 3D position and normal information with chunked processing for efficiency.
 * 
 * @ingroup texturing
 */
class FastLayerTexture : public IterationsProgress
{
public:
    /** Pointer type */
    using Pointer = std::shared_ptr<FastLayerTexture>;
    /** Return type */
    using Texture = std::vector<cv::Mat>;

    /** @brief Default constructor */
    FastLayerTexture() = default;

    /** @brief Create a new FastLayerTexture */
    static auto New() -> Pointer;

    /**
     * @brief Set the per-pixel map
     */
    void setPerPixelMap(const PerPixelMap::Pointer& ppm) { ppm_ = ppm; }

    /**
     * @brief Set the volume
     */
    void setVolume(const Volume::Pointer& volume) { vol_ = volume; }

    /**
     * @brief Set the neighborhood generator
     */
    void setGenerator(const NeighborhoodGenerator::Pointer& gen) { gen_ = gen; }

    /**
     * @brief Set the cache size for zarr chunk reading
     */
    void setCacheSize(std::size_t size) { cacheSize_ = size; }

    /**
     * @brief Set the processing chunk size (default: 1024)
     */
    void setChunkSize(int size) { chunkSize_ = size; }

    /**
     * @brief Enable or disable normal orientation (default: true)
     */
    void setOrientNormals(bool orient) { orientNormals_ = orient; }
    
    /**
     * @brief Get the normal orientation setting
     */
    bool getOrientNormals() const { return orientNormals_; }
    
    /**
     * @brief Get the chunk size setting
     */
    int getChunkSize() const { return chunkSize_; }

    /**
     * @brief Compute the layer textures
     * @return Vector of layer images
     */
    auto compute() -> Texture;

    /**
     * @brief Get the number of progress iterations
     * @return Total number of chunks to process
     */
    std::size_t progressIterations() const override;

private:
    /** Per-pixel map */
    PerPixelMap::Pointer ppm_;
    /** Volume */
    Volume::Pointer vol_;
    /** Neighborhood generator */
    NeighborhoodGenerator::Pointer gen_;
    /** Cache size */
    std::size_t cacheSize_{2'000'000'000};
    /** Processing chunk size */
    int chunkSize_{1024};
    /** Whether to auto-orient normals */
    bool orientNormals_{true};
    /** Result texture layers */
    Texture result_;
    
    // Cached PPM data
    cv::Mat_<cv::Vec3d> cachedPositions_;
    cv::Mat_<cv::Vec3d> cachedNormals_;
    cv::Mat_<uint8_t> cachedValidMask_;
    
    /**
     * @brief Cache PPM data for faster access
     */
    void cachePPMData();
    
    /**
     * @brief Process a single chunk of the output image
     */
    void processChunk(
        z5::Dataset* dataset,
        ChunkCache* cache,
        const cv::Rect& chunk,
        const cv::Size& outputSize,
        std::vector<cv::Mat>& outputs,
        float scaleCoordinates);
};

}  // namespace volcart::texturing
