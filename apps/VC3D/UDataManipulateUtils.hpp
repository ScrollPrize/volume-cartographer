// UDataManipulateUtils.h
// Chao Du 2014 Dec
#pragma once

#include <qimage.h>

#include <opencv2/core.hpp>

namespace ChaoVis
{

cv::Mat QImage2Mat(const QImage& nSrc);

QImage Mat2QImage(const cv::Mat& nSrc);

}  // namespace ChaoVis
