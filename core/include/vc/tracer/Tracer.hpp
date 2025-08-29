#pragma once
#include "vc/core/util/Surface.hpp"

QuadSurface *grow_surf_from_surfs(SurfaceMeta *seed, const std::vector<SurfaceMeta*> &surfs_v, const nlohmann::json &params, float voxelsize = 1.0);
QuadSurface *space_tracing_quad_phys(z5::Dataset *ds, float scale, ChunkCache *cache, cv::Vec3f origin, int generations = 100, float step = 10, const std::string &cache_root = "", float voxelsize = 1.0, std::vector<std::unique_ptr<z5::Dataset>> const &h_fiber_ds = {}, float fibers_scale = 1.f);
