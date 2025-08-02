#pragma once

#include "vc/core/util/VCCollection.hpp"
#include "vc/core/util/Surface.hpp"

namespace vc::apps
{

double point_winding_error(const ChaoVis::VCCollection& collection, const QuadSurface* surface);

} // namespace vc::apps