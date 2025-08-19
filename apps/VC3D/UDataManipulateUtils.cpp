// UDataManipulateUtils.cpp
// Chao Du 2014 Dec
#include <cstddef>
#include <cstdint>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "UDataManipulateUtils.hpp"

namespace ChaoVis
{

// Convert from QImage to cv::Mat
auto QImage2Mat(const QImage& nSrc) -> cv::Mat
{
    cv::Mat tmp(
        nSrc.height(), nSrc.width(), CV_8UC3, const_cast<uchar*>(nSrc.bits()),
        nSrc.bytesPerLine());
    cv::Mat result;  // deep copy
    cvtColor(tmp, result, cv::COLOR_BGR2RGB);
    return result;
}

QImage Mat2QImage(const cv::Mat& src)
{
    if (src.type() == CV_8UC1) {
        // Grayscale
        QImage qimg(src.data, src.cols, src.rows, src.step, QImage::Format_Grayscale8);
        return qimg.copy();
    }
    else if (src.type() == CV_8UC3) {
        // RGB - convert to RGBA for consistency
        cv::Mat rgba;
        cv::cvtColor(src, rgba, cv::COLOR_RGB2RGBA);
        QImage qimg(rgba.data, rgba.cols, rgba.rows, rgba.step, QImage::Format_RGBA8888);
        return qimg.copy();
    }
    else if (src.type() == CV_8UC4) {
        // RGBA
        QImage qimg(src.data, src.cols, src.rows, src.step, QImage::Format_RGBA8888);
        return qimg.copy();
    }
    else {
        return QImage();
    }
}

}  // namespace ChaoVis
