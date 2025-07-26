#pragma once

#include <qimage.h>

#include <opencv2/core.hpp>



bool SplitVertexAndElementBuffer(
    int nVertexNum,
    int nFaceNum,
    const int* nElementBufferTmp,  // constant data, not constant pointer
    unsigned short*** nElementBufferData,
    const float* nVertexBufferTmp,
    float*** nVertexBufferData,
    const float* nUVBufferTmp,
    float*** nUVBufferData,
    int** nElementBufferSize,
    int** nVertexBufferSize,
    int** nUVBufferSize,
    int* nElementArrayNum);

cv::Mat QImage2Mat(const QImage& nSrc);

QImage Mat2QImage(const cv::Mat& nSrc);

