#pragma once

#include "vc/core/util/VCCollection.hpp"
#include "vc/core/util/Surface.hpp"

#include <nlohmann/json.hpp>

#include <opencv2/core.hpp>

nlohmann::json calc_point_metrics(const VCCollection& collection, QuadSurface* surface, const cv::Mat_<float>& winding);
