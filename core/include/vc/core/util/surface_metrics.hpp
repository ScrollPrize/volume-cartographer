#pragma once

#include "vc/core/util/VCCollection.hpp"
#include "vc/core/util/Surface.hpp"

namespace vc::apps
{

#include <opencv2/core.hpp>

double point_winding_error(const ChaoVis::VCCollection& collection, QuadSurface* surface, const cv::Mat_<float>& winding);

} // namespace vc::apps