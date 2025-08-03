#pragma once

#include "vc/core/util/VCCollection.hpp"
#include "vc/core/util/Surface.hpp"

#include <nlohmann/json.hpp>

namespace vc::apps
{

#include <opencv2/core.hpp>

nlohmann::json calc_point_metrics(const ChaoVis::VCCollection& collection, QuadSurface* surface, const cv::Mat_<float>& winding);

} // namespace vc::apps