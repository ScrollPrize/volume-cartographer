#include "vc/core/util/SurfaceVoxelizer.hpp"
#include "vc/core/util/Surface.hpp"
#include <z5/factory.hxx>
#include <z5/filesystem/handle.hxx>
#include <z5/multiarray/xtensor_access.hxx>
#include <z5/attributes.hxx>
#include <nlohmann/json.hpp>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <omp.h>
#include <immintrin.h>
#include <unordered_set>

// Thread-local memory pools
thread_local std::vector<cv::Vec3f> quadPointsBuffer;
thread_local std::vector<std::tuple<int, int, int>> voxelBuffer;
thread_local std::vector<float> interpolationWeights;

// SIMD helper functions
inline __m256 fast_interpolate_avx2(const __m256& a, const __m256& b, const __m256& t) {
    return _mm256_fmadd_ps(t, _mm256_sub_ps(b, a), a);
}

using namespace volcart;

// QuadIndex implementation
void SurfaceVoxelizer::QuadIndex::build(QuadSurface* surface, int targetGridSize) {
    cv::Mat_<cv::Vec3f> points = surface->rawPoints();
    
    // Find bounding box of entire surface
    minBound = cv::Vec3f(FLT_MAX, FLT_MAX, FLT_MAX);
    maxBound = cv::Vec3f(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    
    for (int j = 0; j < points.rows; j++) {
        for (int i = 0; i < points.cols; i++) {
            const cv::Vec3f& p = points(j, i);
            if (p[0] != -1) {  // Valid point
                minBound[0] = std::min(minBound[0], p[0]);
                minBound[1] = std::min(minBound[1], p[1]);
                minBound[2] = std::min(minBound[2], p[2]);
                maxBound[0] = std::max(maxBound[0], p[0]);
                maxBound[1] = std::max(maxBound[1], p[1]);
                maxBound[2] = std::max(maxBound[2], p[2]);
            }
        }
    }
    
    // Expand bounds slightly to avoid edge cases
    cv::Vec3f expansion(1.0f, 1.0f, 1.0f);
    minBound -= expansion;
    maxBound += expansion;
    
    // Setup grid
    gridSize = targetGridSize;
    cellSize = (maxBound - minBound) / static_cast<float>(gridSize);
    grid.clear();
    grid.resize(gridSize * gridSize * gridSize);
    
    // Insert quads into grid
    for (int j = 0; j < points.rows - 1; j++) {
        for (int i = 0; i < points.cols - 1; i++) {
            const cv::Vec3f& p00 = points(j, i);
            const cv::Vec3f& p01 = points(j, i+1);
            const cv::Vec3f& p10 = points(j+1, i);
            const cv::Vec3f& p11 = points(j+1, i+1);
            
            // Skip invalid quads
            if (p00[0] == -1 || p01[0] == -1 || p10[0] == -1 || p11[0] == -1)
                continue;
            
            // Compute quad bounds
            QuadEntry entry;
            entry.i = i;
            entry.j = j;
            entry.minBound = cv::Vec3f(
                std::min({p00[0], p01[0], p10[0], p11[0]}),
                std::min({p00[1], p01[1], p10[1], p11[1]}),
                std::min({p00[2], p01[2], p10[2], p11[2]})
            );
            entry.maxBound = cv::Vec3f(
                std::max({p00[0], p01[0], p10[0], p11[0]}),
                std::max({p00[1], p01[1], p10[1], p11[1]}),
                std::max({p00[2], p01[2], p10[2], p11[2]})
            );
            
            // Find grid cells that this quad overlaps
            int minGX = std::max(0, static_cast<int>((entry.minBound[0] - minBound[0]) / cellSize[0]));
            int minGY = std::max(0, static_cast<int>((entry.minBound[1] - minBound[1]) / cellSize[1]));
            int minGZ = std::max(0, static_cast<int>((entry.minBound[2] - minBound[2]) / cellSize[2]));
            int maxGX = std::min(gridSize - 1, static_cast<int>((entry.maxBound[0] - minBound[0]) / cellSize[0]));
            int maxGY = std::min(gridSize - 1, static_cast<int>((entry.maxBound[1] - minBound[1]) / cellSize[1]));
            int maxGZ = std::min(gridSize - 1, static_cast<int>((entry.maxBound[2] - minBound[2]) / cellSize[2]));
            
            // Insert into all overlapping cells
            for (int gz = minGZ; gz <= maxGZ; gz++) {
                for (int gy = minGY; gy <= maxGY; gy++) {
                    for (int gx = minGX; gx <= maxGX; gx++) {
                        int idx = gz * gridSize * gridSize + gy * gridSize + gx;
                        grid[idx].push_back(entry);
                    }
                }
            }
        }
    }
}

std::vector<SurfaceVoxelizer::QuadIndex::QuadEntry> 
SurfaceVoxelizer::QuadIndex::getQuadsInRegion(const cv::Vec3f& minRegion, const cv::Vec3f& maxRegion) const {
    std::vector<QuadEntry> result;
    std::unordered_set<uint32_t> seen;  // To avoid duplicates (encode i,j as single int)
    
    // Find grid cells that overlap with region
    int minGX = std::max(0, static_cast<int>((minRegion[0] - minBound[0]) / cellSize[0]));
    int minGY = std::max(0, static_cast<int>((minRegion[1] - minBound[1]) / cellSize[1]));
    int minGZ = std::max(0, static_cast<int>((minRegion[2] - minBound[2]) / cellSize[2]));
    int maxGX = std::min(gridSize - 1, static_cast<int>((maxRegion[0] - minBound[0]) / cellSize[0]));
    int maxGY = std::min(gridSize - 1, static_cast<int>((maxRegion[1] - minBound[1]) / cellSize[1]));
    int maxGZ = std::min(gridSize - 1, static_cast<int>((maxRegion[2] - minBound[2]) / cellSize[2]));
    
    // Collect quads from overlapping cells
    for (int gz = minGZ; gz <= maxGZ; gz++) {
        for (int gy = minGY; gy <= maxGY; gy++) {
            for (int gx = minGX; gx <= maxGX; gx++) {
                int idx = gz * gridSize * gridSize + gy * gridSize + gx;
                for (const auto& entry : grid[idx]) {
                    // Check if quad actually overlaps with region
                    if (entry.maxBound[0] >= minRegion[0] && entry.minBound[0] <= maxRegion[0] &&
                        entry.maxBound[1] >= minRegion[1] && entry.minBound[1] <= maxRegion[1] &&
                        entry.maxBound[2] >= minRegion[2] && entry.minBound[2] <= maxRegion[2]) {
                        
                        uint32_t key = (entry.j << 16) | entry.i;
                        if (seen.insert(key).second) {
                            result.push_back(entry);
                        }
                    }
                }
            }
        }
    }
    
    return result;
}

// Fast triangle rasterization for gap filling
void rasterizeTriangle(
    std::vector<std::tuple<int, int, int>>& voxelBuffer,
    const cv::Vec3f& v0, const cv::Vec3f& v1, const cv::Vec3f& v2,
    const cv::Vec3i& chunkSize)
{
    // Compute 2D bounding box
    int minX = std::max(0, static_cast<int>(std::floor(std::min({v0[0], v1[0], v2[0]}))) - 1);
    int maxX = std::min(chunkSize[0] - 1, static_cast<int>(std::ceil(std::max({v0[0], v1[0], v2[0]}))) + 1);
    int minY = std::max(0, static_cast<int>(std::floor(std::min({v0[1], v1[1], v2[1]}))) - 1);
    int maxY = std::min(chunkSize[1] - 1, static_cast<int>(std::ceil(std::max({v0[1], v1[1], v2[1]}))) + 1);
    
    // Skip degenerate triangles
    if (minX > maxX || minY > maxY) return;
    
    // Edge function for point-in-triangle test
    auto edgeFunction = [](const cv::Vec2f& a, const cv::Vec2f& b, const cv::Vec2f& c) {
        return (c[0] - a[0]) * (b[1] - a[1]) - (c[1] - a[1]) * (b[0] - a[0]);
    };
    
    cv::Vec2f p0(v0[0], v0[1]);
    cv::Vec2f p1(v1[0], v1[1]);
    cv::Vec2f p2(v2[0], v2[1]);
    
    // Precompute triangle area
    float area = edgeFunction(p0, p1, p2);
    if (std::abs(area) < 0.01f) return; // Skip degenerate triangles
    
    // Scan through bounding box
    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            cv::Vec2f p(x + 0.5f, y + 0.5f);
            
            // Barycentric coordinates
            float w0 = edgeFunction(p1, p2, p);
            float w1 = edgeFunction(p2, p0, p);
            float w2 = edgeFunction(p0, p1, p);
            
            // Check if inside triangle
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                // Interpolate Z
                w0 /= area;
                w1 /= area;
                w2 /= area;
                float z = w0 * v0[2] + w1 * v1[2] + w2 * v2[2];
                
                int vz = static_cast<int>(std::round(z));
                if (vz >= 0 && vz < chunkSize[2]) {
                    voxelBuffer.emplace_back(x, y, vz);
                }
            }
        }
    }
}

void SurfaceVoxelizer::voxelizeSurfaces(
    const std::string& outputPath,
    const std::map<std::string, QuadSurface*>& surfaces,
    const VolumeInfo& volumeInfo,
    const VoxelizationParams& params,
    std::function<void(int)> progressCallback)
{
    if (surfaces.empty()) {
        throw std::runtime_error("No surfaces provided for voxelization");
    }
    
    // Use the provided volume dimensions
    size_t nx = volumeInfo.width;
    size_t ny = volumeInfo.height;
    size_t nz = volumeInfo.depth;
    
    std::cout << "Creating voxel grid with volume dimensions: " 
              << nx << " x " << ny << " x " << nz << std::endl;
    std::cout << "Voxel size: " << volumeInfo.voxelSize << " mm" << std::endl;
    std::cout << "Using chunk-based processing with chunk size: " << params.chunkSize << std::endl;
    
    // Create zarr file structure
    z5::filesystem::handle::File zarrFile(outputPath);
    z5::createFile(zarrFile, true); // true = zarr format
    
    // Create base resolution dataset
    std::vector<size_t> shape = {nz, ny, nx};
    std::vector<size_t> chunks = {
        static_cast<size_t>(params.chunkSize), 
        static_cast<size_t>(params.chunkSize), 
        static_cast<size_t>(params.chunkSize)
    };
    
    // Adjust chunk size if smaller than grid dimensions
    for (size_t i = 0; i < 3; ++i) {
        chunks[i] = std::min(chunks[i], shape[i]);
    }
    
    auto ds0 = z5::createDataset(zarrFile, "0", "uint8", shape, chunks);
    
    // Calculate total number of chunks for progress tracking
    size_t totalChunks = 0;
    for (size_t cz = 0; cz < nz; cz += params.chunkSize) {
        for (size_t cy = 0; cy < ny; cy += params.chunkSize) {
            for (size_t cx = 0; cx < nx; cx += params.chunkSize) {
                totalChunks++;
            }
        }
    }
    
    std::atomic<int> completedChunks(0);
    
    // Create a list of all chunk coordinates to process
    std::vector<std::array<size_t, 3>> chunkCoords;
    for (size_t cz = 0; cz < nz; cz += params.chunkSize) {
        for (size_t cy = 0; cy < ny; cy += params.chunkSize) {
            for (size_t cx = 0; cx < nx; cx += params.chunkSize) {
                chunkCoords.push_back({cx, cy, cz});
            }
        }
    }
    
    // Process chunks in parallel with guided scheduling for better load balancing
    #pragma omp parallel for schedule(guided, 4)
    for (size_t i = 0; i < chunkCoords.size(); ++i) {
        size_t cx = chunkCoords[i][0];
        size_t cy = chunkCoords[i][1];
        size_t cz = chunkCoords[i][2];
        
        // Calculate actual chunk dimensions
        size_t chunk_nx = std::min(static_cast<size_t>(params.chunkSize), nx - cx);
        size_t chunk_ny = std::min(static_cast<size_t>(params.chunkSize), ny - cy);
        size_t chunk_nz = std::min(static_cast<size_t>(params.chunkSize), nz - cz);
        
        // Create chunk bounds for intersection testing
        Rect3D chunkBounds = {
            cv::Vec3f(static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(cz)),
            cv::Vec3f(static_cast<float>(cx + chunk_nx), static_cast<float>(cy + chunk_ny), static_cast<float>(cz + chunk_nz))
        };
        
        // Find surfaces that intersect this chunk
        std::vector<QuadSurface*> relevantSurfaces;
        for (const auto& [name, surface] : surfaces) {
            if (!surface) continue;
            
            Rect3D surfaceBBox = surface->bbox();
            if (intersect(surfaceBBox, chunkBounds)) {
                relevantSurfaces.push_back(surface);
            }
        }
        
        // Skip empty chunks
        if (relevantSurfaces.empty()) {
            int completed = completedChunks.fetch_add(1) + 1;
            if (progressCallback) {
                int progress = (completed * 100) / totalChunks;
                progressCallback(progress);
            }
            continue;
        }
        
        // Allocate chunk memory
        xt::xarray<uint8_t> chunk = xt::zeros<uint8_t>({chunk_nz, chunk_ny, chunk_nx});
        
        // Process each relevant surface directly
        cv::Vec3i chunkOffset(cx, cy, cz);
        cv::Vec3i chunkSize(chunk_nx, chunk_ny, chunk_nz);
        
        for (QuadSurface* surface : relevantSurfaces) {
            voxelizeSurfaceChunk(surface, chunk, chunkOffset, chunkSize, params, nullptr);
        }
        
        // Write chunk to zarr
        z5::types::ShapeType offset = {cz, cy, cx};
        z5::multiarray::writeSubarray<uint8_t>(ds0, chunk, offset.begin());
        
        // Update progress
        int completed = completedChunks.fetch_add(1) + 1;
        if (progressCallback) {
            int progress = (completed * 100) / totalChunks;
            progressCallback(progress);
        }
    }
    
    // Create multi-resolution pyramid
    createPyramid(zarrFile, shape);
    
    // Write metadata
    nlohmann::json attrs;
    attrs["surfaces"] = nlohmann::json::array();
    for (const auto& [name, _] : surfaces) {
        attrs["surfaces"].push_back(name);
    }
    attrs["voxel_size"] = volumeInfo.voxelSize;
    attrs["volume_dimensions"] = {nx, ny, nz};
    
    // Get current time as ISO string
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    attrs["created"] = ss.str();
    
    z5::writeAttributes(zarrFile, attrs);
}

void SurfaceVoxelizer::voxelizeSurfaceChunk(
    QuadSurface* surface,
    xt::xarray<uint8_t>& chunk,
    const cv::Vec3i& chunkOffset,
    const cv::Vec3i& chunkSize,
    const VoxelizationParams& params,
    const QuadIndex* spatialIndex)
{
    cv::Mat_<cv::Vec3f> points = surface->rawPoints();
    
    // Calculate sampling step based on density parameter
    const float step = params.samplingDensity;
    
    // Pre-calculate chunk bounds for faster checking
    const float chunkMinX = static_cast<float>(chunkOffset[0]);
    const float chunkMaxX = static_cast<float>(chunkOffset[0] + chunkSize[0]);
    const float chunkMinY = static_cast<float>(chunkOffset[1]);
    const float chunkMaxY = static_cast<float>(chunkOffset[1] + chunkSize[1]);
    const float chunkMinZ = static_cast<float>(chunkOffset[2]);
    const float chunkMaxZ = static_cast<float>(chunkOffset[2] + chunkSize[2]);
    
    // Pre-compute extended bounds for sample collection
    const float extMinX = chunkMinX - 1.5f;
    const float extMaxX = chunkMaxX + 1.5f;
    const float extMinY = chunkMinY - 1.5f;
    const float extMaxY = chunkMaxY + 1.5f;
    const float extMinZ = chunkMinZ - 1.5f;
    const float extMaxZ = chunkMaxZ + 1.5f;
    
    // Reuse thread-local buffers
    quadPointsBuffer.clear();
    quadPointsBuffer.reserve(512);
    voxelBuffer.clear();
    voxelBuffer.reserve(2048);
    
    // Build a lightweight spatial index just for this chunk region
    // First pass: count quads in extended region
    int quadCount = 0;
    const float searchPadding = 10.0f; // Padding around chunk
    for (int j = 0; j < points.rows - 1; j++) {
        for (int i = 0; i < points.cols - 1; i++) {
            const cv::Vec3f& p00 = points(j, i);
            if (p00[0] == -1) continue;
            const cv::Vec3f& p11 = points(j+1, i+1);
            if (p11[0] == -1) continue;
            
            // Quick bounds check with padding
            if (p11[0] >= chunkMinX - searchPadding && p00[0] <= chunkMaxX + searchPadding &&
                p11[1] >= chunkMinY - searchPadding && p00[1] <= chunkMaxY + searchPadding &&
                p11[2] >= chunkMinZ - searchPadding && p00[2] <= chunkMaxZ + searchPadding) {
                quadCount++;
            }
        }
    }
    
    // If there are many quads, use spatial subdivision for this chunk
    bool useLocalIndex = quadCount > 1000;
    std::vector<std::pair<int, int>> relevantQuadIndices;
    
    if (useLocalIndex) {
        // Build a simple grid index just for quads near this chunk
        const int localGridSize = 8; // Smaller grid just for chunk region
        cv::Vec3f localMin(chunkMinX - searchPadding, chunkMinY - searchPadding, chunkMinZ - searchPadding);
        cv::Vec3f localMax(chunkMaxX + searchPadding, chunkMaxY + searchPadding, chunkMaxZ + searchPadding);
        cv::Vec3f localCellSize = (localMax - localMin) / static_cast<float>(localGridSize);
        
        std::vector<std::vector<std::pair<int, int>>> localGrid(localGridSize * localGridSize * localGridSize);
        
        // Insert quads into local grid
        for (int j = 0; j < points.rows - 1; j++) {
            for (int i = 0; i < points.cols - 1; i++) {
                const cv::Vec3f& p00 = points(j, i);
                const cv::Vec3f& p01 = points(j, i+1);
                const cv::Vec3f& p10 = points(j+1, i);
                const cv::Vec3f& p11 = points(j+1, i+1);
                
                if (p00[0] == -1 || p01[0] == -1 || p10[0] == -1 || p11[0] == -1)
                    continue;
                
                float minX = std::min({p00[0], p01[0], p10[0], p11[0]});
                float maxX = std::max({p00[0], p01[0], p10[0], p11[0]});
                float minY = std::min({p00[1], p01[1], p10[1], p11[1]});
                float maxY = std::max({p00[1], p01[1], p10[1], p11[1]});
                float minZ = std::min({p00[2], p01[2], p10[2], p11[2]});
                float maxZ = std::max({p00[2], p01[2], p10[2], p11[2]});
                
                // Check if quad is in extended region
                if (maxX < localMin[0] || minX > localMax[0] ||
                    maxY < localMin[1] || minY > localMax[1] ||
                    maxZ < localMin[2] || minZ > localMax[2])
                    continue;
                
                // Find grid cells this quad overlaps
                int gxMin = std::max(0, static_cast<int>((minX - localMin[0]) / localCellSize[0]));
                int gyMin = std::max(0, static_cast<int>((minY - localMin[1]) / localCellSize[1]));
                int gzMin = std::max(0, static_cast<int>((minZ - localMin[2]) / localCellSize[2]));
                int gxMax = std::min(localGridSize - 1, static_cast<int>((maxX - localMin[0]) / localCellSize[0]));
                int gyMax = std::min(localGridSize - 1, static_cast<int>((maxY - localMin[1]) / localCellSize[1]));
                int gzMax = std::min(localGridSize - 1, static_cast<int>((maxZ - localMin[2]) / localCellSize[2]));
                
                for (int gz = gzMin; gz <= gzMax; gz++) {
                    for (int gy = gyMin; gy <= gyMax; gy++) {
                        for (int gx = gxMin; gx <= gxMax; gx++) {
                            int idx = gz * localGridSize * localGridSize + gy * localGridSize + gx;
                            localGrid[idx].push_back({i, j});
                        }
                    }
                }
            }
        }
        
        // Query the local grid for quads in chunk region
        std::unordered_set<uint32_t> seen;
        int gxMin = std::max(0, static_cast<int>((chunkMinX - localMin[0]) / localCellSize[0]));
        int gyMin = std::max(0, static_cast<int>((chunkMinY - localMin[1]) / localCellSize[1]));
        int gzMin = std::max(0, static_cast<int>((chunkMinZ - localMin[2]) / localCellSize[2]));
        int gxMax = std::min(localGridSize - 1, static_cast<int>((chunkMaxX - localMin[0]) / localCellSize[0]));
        int gyMax = std::min(localGridSize - 1, static_cast<int>((chunkMaxY - localMin[1]) / localCellSize[1]));
        int gzMax = std::min(localGridSize - 1, static_cast<int>((chunkMaxZ - localMin[2]) / localCellSize[2]));
        
        for (int gz = gzMin; gz <= gzMax; gz++) {
            for (int gy = gyMin; gy <= gyMax; gy++) {
                for (int gx = gxMin; gx <= gxMax; gx++) {
                    int idx = gz * localGridSize * localGridSize + gy * localGridSize + gx;
                    for (const auto& [i, j] : localGrid[idx]) {
                        uint32_t key = (j << 16) | i;
                        if (seen.insert(key).second) {
                            relevantQuadIndices.push_back({i, j});
                        }
                    }
                }
            }
        }
    }
    
    // Process quads using the index or direct iteration
    auto processQuad = [&](int i, int j) {
        // Get quad corners
        const cv::Vec3f& p00 = points(j, i);
        const cv::Vec3f& p01 = points(j, i+1);
        const cv::Vec3f& p10 = points(j+1, i);
        const cv::Vec3f& p11 = points(j+1, i+1);
        
        // Skip invalid quads
        if (p00[0] == -1 || p01[0] == -1 || p10[0] == -1 || p11[0] == -1)
            return;
        
        // Quick bounds check - skip if quad is entirely outside chunk
        float minX = std::min({p00[0], p01[0], p10[0], p11[0]});
        float maxX = std::max({p00[0], p01[0], p10[0], p11[0]});
        if (maxX < chunkMinX || minX >= chunkMaxX)
            return;
            
        float minY = std::min({p00[1], p01[1], p10[1], p11[1]});
        float maxY = std::max({p00[1], p01[1], p10[1], p11[1]});
        if (maxY < chunkMinY || minY >= chunkMaxY)
            return;
            
        float minZ = std::min({p00[2], p01[2], p10[2], p11[2]});
        float maxZ = std::max({p00[2], p01[2], p10[2], p11[2]});
        if (maxZ < chunkMinZ || minZ >= chunkMaxZ)
            return;
        
        // Adaptive sampling based on quad size and flatness
        const float quadDiag = std::max(cv::norm(p11 - p00), cv::norm(p10 - p01));
        const cv::Vec3f normal = (p11 + p10 + p01 + p00) * 0.25f;
        const float flatness = std::abs(p00[2] - normal[2]) + std::abs(p01[2] - normal[2]) + 
                               std::abs(p10[2] - normal[2]) + std::abs(p11[2] - normal[2]);
        
        // Adjust step based on quad size and flatness
        float adaptiveStep = step;
        if (flatness < 0.5f && quadDiag < 3.0f) {
            adaptiveStep = std::min(0.8f, step * 2.0f); // Coarser sampling for flat, small quads
        } else {
            adaptiveStep = std::min(step, 1.0f / std::max(2.0f, quadDiag));
        }
        
        const int numU = std::max(2, static_cast<int>(1.0f / adaptiveStep) + 1);
        const int numV = std::max(2, static_cast<int>(1.0f / adaptiveStep) + 1);
        
        // Clear and prepare buffer
        quadPointsBuffer.clear();
        
        // Pre-compute reciprocals for faster division
        const float invNumU = 1.0f / numU;
        const float invNumV = 1.0f / numV;
        
        // Pre-compute interpolation weights
        interpolationWeights.resize((numU + 1) * 2);
        for (int ui = 0; ui <= numU; ui++) {
            const float u = static_cast<float>(ui) * invNumU;
            interpolationWeights[ui * 2] = 1.0f - u;
            interpolationWeights[ui * 2 + 1] = u;
        }
        
        // Generate sample points with optimized interpolation
        for (int vi = 0; vi <= numV; vi++) {
            const float v = static_cast<float>(vi) * invNumV;
            const float v1 = 1.0f - v;
            
            // Bilinear interpolation for edge points
            const cv::Vec3f p0 = v1 * p00 + v * p10;
            const cv::Vec3f p1 = v1 * p01 + v * p11;
            
            // Use pre-computed weights
            for (int ui = 0; ui <= numU; ui++) {
                const float w0 = interpolationWeights[ui * 2];
                const float w1 = interpolationWeights[ui * 2 + 1];
                const cv::Vec3f p = w0 * p0 + w1 * p1;
                
                // Only store points that might be in chunk
                if (p[0] >= extMinX && p[0] < extMaxX &&
                    p[1] >= extMinY && p[1] < extMaxY &&
                    p[2] >= extMinZ && p[2] < extMaxZ) {
                    quadPointsBuffer.push_back(p);
                }
            }
        }
        
        // Collect voxel coordinates first (for better memory access patterns)
        const int offsetX = chunkOffset[0];
        const int offsetY = chunkOffset[1];
        const int offsetZ = chunkOffset[2];
        
        for (const auto& p : quadPointsBuffer) {
            // Fast voxel coordinate conversion with rounding
            const int vx = static_cast<int>(std::round(p[0])) - offsetX;
            const int vy = static_cast<int>(std::round(p[1])) - offsetY;
            const int vz = static_cast<int>(std::round(p[2])) - offsetZ;
            
            // Bounds check within chunk
            if (vx >= 0 && vx < chunkSize[0] &&
                vy >= 0 && vy < chunkSize[1] &&
                vz >= 0 && vz < chunkSize[2]) {
                voxelBuffer.emplace_back(vx, vy, vz);
            }
        }
        
        // Optimized gap filling with triangle rasterization
        if (params.fillGaps && quadDiag > 1.5f) {
            // Convert quad corners to chunk coordinates once
            const cv::Vec3f c00 = p00 - cv::Vec3f(offsetX, offsetY, offsetZ);
            const cv::Vec3f c01 = p01 - cv::Vec3f(offsetX, offsetY, offsetZ);
            const cv::Vec3f c10 = p10 - cv::Vec3f(offsetX, offsetY, offsetZ);
            const cv::Vec3f c11 = p11 - cv::Vec3f(offsetX, offsetY, offsetZ);
            
            // Rasterize as two triangles for better coverage
            rasterizeTriangle(voxelBuffer, c00, c01, c11, chunkSize);
            rasterizeTriangle(voxelBuffer, c00, c11, c10, chunkSize);
        }
    };
    
    if (useLocalIndex) {
        // Process only the relevant quads from the index
        for (const auto& [i, j] : relevantQuadIndices) {
            processQuad(i, j);
        }
    } else {
        // Process all quads with direct iteration
        for (int j = 0; j < points.rows - 1; j++) {
            for (int i = 0; i < points.cols - 1; i++) {
                processQuad(i, j);
            }
        }
    }
    
    // Sort voxels for better cache locality during write
    std::sort(voxelBuffer.begin(), voxelBuffer.end(),
              [](const auto& a, const auto& b) {
                  // Sort by Z, then Y, then X for memory layout
                  if (std::get<2>(a) != std::get<2>(b)) return std::get<2>(a) < std::get<2>(b);
                  if (std::get<1>(a) != std::get<1>(b)) return std::get<1>(a) < std::get<1>(b);
                  return std::get<0>(a) < std::get<0>(b);
              });
    
    // Write voxels with deduplication
    int lastX = -1, lastY = -1, lastZ = -1;
    for (const auto& [vx, vy, vz] : voxelBuffer) {
        if (vx != lastX || vy != lastY || vz != lastZ) {
            chunk(vz, vy, vx) = 255;
            lastX = vx; lastY = vy; lastZ = vz;
        }
    }
}

void SurfaceVoxelizer::drawLine3D(
    xt::xarray<uint8_t>& grid,
    const cv::Vec3f& start,
    const cv::Vec3f& end)
{
    // Optimized 3D DDA line algorithm
    const float length = cv::norm(end - start);
    if (length < 0.01f) return;
    
    const int steps = static_cast<int>(std::ceil(length * 1.5f));
    const cv::Vec3f step = (end - start) / static_cast<float>(steps);
    
    cv::Vec3f pos = start;
    const int maxX = grid.shape(2);
    const int maxY = grid.shape(1);
    const int maxZ = grid.shape(0);
    
    for (int i = 0; i <= steps; i++) {
        const int x = static_cast<int>(std::round(pos[0]));
        const int y = static_cast<int>(std::round(pos[1]));
        const int z = static_cast<int>(std::round(pos[2]));
        
        // Bounds check and set voxel
        if (x >= 0 && x < maxX && y >= 0 && y < maxY && z >= 0 && z < maxZ) {
            grid(z, y, x) = 255;
        }
        
        pos += step;
    }
}

void SurfaceVoxelizer::connectEdge(
    xt::xarray<uint8_t>& grid,
    const cv::Vec3f& p0,
    const cv::Vec3f& p1,
    const VoxelizationParams& params)
{
    // Sample along the edge
    float length = cv::norm(p1 - p0);
    int numSamples = std::ceil(length / params.samplingDensity);
    if (numSamples < 2) numSamples = 2;
    
    cv::Vec3f lastPoint = p0;
    for (int i = 0; i <= numSamples; ++i) {
        float t = static_cast<float>(i) / numSamples;
        cv::Vec3f point = (1 - t) * p0 + t * p1;
        
        if (i > 0) {
            drawLine3D(grid, lastPoint, point);
        }
        lastPoint = point;
    }
}

void SurfaceVoxelizer::createPyramid(
    z5::filesystem::handle::File& zarrFile,
    const std::vector<size_t>& baseShape)
{
    // Create datasets for levels 1-4
    for (int level = 1; level < 5; level++) {
        int scale = 1 << level; // 2, 4, 8, 16
        
        std::vector<size_t> shape = {
            (baseShape[0] + scale - 1) / scale,
            (baseShape[1] + scale - 1) / scale,
            (baseShape[2] + scale - 1) / scale
        };
        
        std::vector<size_t> chunks = {128, 128, 128};
        // Adjust chunk size if smaller than shape
        for (size_t i = 0; i < 3; ++i) {
            chunks[i] = std::min(chunks[i], shape[i]);
        }
        
        auto ds = z5::createDataset(zarrFile, std::to_string(level), 
                                   "uint8", shape, chunks);
        
        // Downsample from previous level
        downsampleLevel(zarrFile, level);
    }
}

void SurfaceVoxelizer::downsampleLevel(
    z5::filesystem::handle::File& zarrFile, 
    int targetLevel)
{
    auto srcDs = z5::openDataset(zarrFile, std::to_string(targetLevel - 1));
    auto dstDs = z5::openDataset(zarrFile, std::to_string(targetLevel));
    
    const auto& srcShape = srcDs->shape();
    const auto& dstShape = dstDs->shape();
    
    // Process in chunks (larger chunks for pyramid levels)
    const size_t chunkSize = 128;
    
    // Create list of chunks to process
    std::vector<std::array<size_t, 3>> pyramidChunkCoords;
    for (size_t dz = 0; dz < dstShape[0]; dz += chunkSize) {
        for (size_t dy = 0; dy < dstShape[1]; dy += chunkSize) {
            for (size_t dx = 0; dx < dstShape[2]; dx += chunkSize) {
                pyramidChunkCoords.push_back({dx, dy, dz});
            }
        }
    }
    
    // Process pyramid chunks in parallel
    #pragma omp parallel for schedule(dynamic)
    for (size_t i = 0; i < pyramidChunkCoords.size(); ++i) {
        size_t dx = pyramidChunkCoords[i][0];
        size_t dy = pyramidChunkCoords[i][1];
        size_t dz = pyramidChunkCoords[i][2];
        
        // Calculate chunk dimensions
        size_t chunk_nz = std::min(chunkSize, dstShape[0] - dz);
        size_t chunk_ny = std::min(chunkSize, dstShape[1] - dy);
        size_t chunk_nx = std::min(chunkSize, dstShape[2] - dx);
        
        // Create output chunk
        xt::xarray<uint8_t> dstChunk = xt::zeros<uint8_t>({chunk_nz, chunk_ny, chunk_nx});
        
        // Read corresponding source region (2x size)
        size_t src_z = dz * 2;
        size_t src_y = dy * 2;
        size_t src_x = dx * 2;
        size_t src_nz = std::min(chunk_nz * 2, srcShape[0] - src_z);
        size_t src_ny = std::min(chunk_ny * 2, srcShape[1] - src_y);
        size_t src_nx = std::min(chunk_nx * 2, srcShape[2] - src_x);
        
        // Create properly sized source chunk
        xt::xarray<uint8_t> srcChunk = xt::zeros<uint8_t>({src_nz, src_ny, src_nx});
        
        // Thread-safe read
        #pragma omp critical(zarr_read_pyramid)
        {
            z5::types::ShapeType srcOffset = {src_z, src_y, src_x};
            z5::multiarray::readSubarray<uint8_t>(srcDs, srcChunk, srcOffset.begin());
        }
        
        // Max-pooling: if any voxel in 2x2x2 block is set, output is set
        for (size_t z = 0; z < chunk_nz; ++z) {
            for (size_t y = 0; y < chunk_ny; ++y) {
                for (size_t x = 0; x < chunk_nx; ++x) {
                    uint8_t maxVal = 0;
                    for (int ddz = 0; ddz < 2 && z*2+ddz < src_nz; ++ddz) {
                        for (int ddy = 0; ddy < 2 && y*2+ddy < src_ny; ++ddy) {
                            for (int ddx = 0; ddx < 2 && x*2+ddx < src_nx; ++ddx) {
                                maxVal = std::max(maxVal, srcChunk(z*2+ddz, y*2+ddy, x*2+ddx));
                            }
                        }
                    }
                    dstChunk(z, y, x) = maxVal;
                }
            }
        }
        
        // Thread-safe write
        #pragma omp critical(zarr_write_pyramid)
        {
            z5::types::ShapeType dstOffset = {dz, dy, dx};
            z5::multiarray::writeSubarray<uint8_t>(dstDs, dstChunk, dstOffset.begin());
        }
    }
}
