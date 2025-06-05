#pragma once

#include <vector>
#include <QString>
#include <QColor>
#include <opencv2/core.hpp>

namespace ChaoVis {

/**
 * @brief Structure representing a drawn path
 * 
 * This structure is used to pass path data between widgets and the volume viewer.
 * Any widget can create paths and emit them for rendering.
 */
struct PathData {
    std::vector<cv::Vec3f> points;  ///< 3D points making up the path
    QColor color;                   ///< Color of the path
    float lineWidth = 3.0f;         ///< Width of the line when rendered
    QString id;                     ///< Unique identifier for the path
    QString ownerWidget;            ///< Widget that created this path
    
    // Optional metadata
    enum class PathType {
        FREEHAND,    ///< Continuous freehand drawing
        POLYLINE,    ///< Connected line segments
        SPLINE       ///< Smooth spline curve
    };
    PathType type = PathType::FREEHAND;
    
    // Constructor
    PathData() = default;
    PathData(const std::vector<cv::Vec3f>& pts, const QColor& col, const QString& owner = "")
        : points(pts), color(col), ownerWidget(owner) {}
};

} // namespace ChaoVis
