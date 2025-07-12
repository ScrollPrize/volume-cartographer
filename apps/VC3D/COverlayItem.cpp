#include "COverlayItem.hpp"
#include <algorithm>
#include <limits>
#include <QPainterPath>

namespace ChaoVis {

// Base COverlayItem implementation
COverlayItem::COverlayItem(Type type, const QString& id)
    : type_(type), id_(id)
{
}

std::pair<cv::Vec3f, cv::Vec3f> COverlayItem::getBoundingBox() const
{
    if (volumeCoords_.empty()) {
        return {cv::Vec3f(0, 0, 0), cv::Vec3f(0, 0, 0)};
    }
    
    cv::Vec3f minPt(std::numeric_limits<float>::max());
    cv::Vec3f maxPt(std::numeric_limits<float>::lowest());
    
    for (const auto& coord : volumeCoords_) {
        minPt[0] = std::min(minPt[0], coord[0]);
        minPt[1] = std::min(minPt[1], coord[1]);
        minPt[2] = std::min(minPt[2], coord[2]);
        
        maxPt[0] = std::max(maxPt[0], coord[0]);
        maxPt[1] = std::max(maxPt[1], coord[1]);
        maxPt[2] = std::max(maxPt[2], coord[2]);
    }
    
    return {minPt, maxPt};
}

// COverlayPoint implementation
COverlayPoint::COverlayPoint(const QString& id)
    : COverlayItem(POINT, id)
{
}

void COverlayPoint::setPoint(const cv::Vec3f& coord)
{
    volumeCoords_.clear();
    volumeCoords_.push_back(coord);
}

cv::Vec3f COverlayPoint::getPoint() const
{
    if (!volumeCoords_.empty()) {
        return volumeCoords_[0];
    }
    return cv::Vec3f(0, 0, 0);
}

std::vector<QGraphicsItem*> COverlayPoint::createGraphicsItems(
    const std::vector<cv::Vec2f>& sceneCoords) const
{
    std::vector<QGraphicsItem*> items;
    
    if (sceneCoords.empty()) {
        return items;
    }
    
    const cv::Vec2f& pt = sceneCoords[0];
    
    // Create the point
    QGraphicsEllipseItem* point = new QGraphicsEllipseItem(
        pt[0] - pointSize_/2, pt[1] - pointSize_/2, 
        pointSize_, pointSize_);
    
    // Set colors with opacity
    QColor fillColor = color_;
    fillColor.setAlphaF(opacity_);
    QColor borderColor = color_.darker(150);
    borderColor.setAlphaF(opacity_);
    
    point->setBrush(QBrush(fillColor));
    point->setPen(QPen(borderColor, 1.0));
    point->setZValue(zOrder_);
    
    items.push_back(point);
    return items;
}

// COverlayLine implementation
COverlayLine::COverlayLine(const QString& id)
    : COverlayItem(LINE, id)
{
}

void COverlayLine::setLine(const cv::Vec3f& start, const cv::Vec3f& end)
{
    volumeCoords_.clear();
    volumeCoords_.push_back(start);
    volumeCoords_.push_back(end);
}

std::pair<cv::Vec3f, cv::Vec3f> COverlayLine::getLine() const
{
    if (volumeCoords_.size() >= 2) {
        return {volumeCoords_[0], volumeCoords_[1]};
    }
    return {cv::Vec3f(0, 0, 0), cv::Vec3f(0, 0, 0)};
}

std::vector<QGraphicsItem*> COverlayLine::createGraphicsItems(
    const std::vector<cv::Vec2f>& sceneCoords) const
{
    std::vector<QGraphicsItem*> items;
    
    if (sceneCoords.size() < 2) {
        return items;
    }
    
    const cv::Vec2f& start = sceneCoords[0];
    const cv::Vec2f& end = sceneCoords[1];
    
    // Create the line
    QGraphicsLineItem* line = new QGraphicsLineItem(
        start[0], start[1], end[0], end[1]);
    
    // Set pen with color and style
    QColor lineColor = color_;
    lineColor.setAlphaF(opacity_);
    QPen pen(lineColor, lineWidth_, lineStyle_);
    pen.setCapStyle(Qt::RoundCap);
    line->setPen(pen);
    line->setZValue(zOrder_);
    
    items.push_back(line);
    
    // Add arrow head if requested
    if (hasArrowHead_) {
        float arrowSize = lineWidth_ * 4;
        
        // Calculate arrow direction
        cv::Vec2f dir = end - start;
        float length = cv::norm(dir);
        if (length > 0) {
            dir /= length;
            
            // Create arrow head triangle
            cv::Vec2f perpDir(-dir[1], dir[0]);
            cv::Vec2f arrowPt1 = end - dir * arrowSize + perpDir * (arrowSize/2);
            cv::Vec2f arrowPt2 = end - dir * arrowSize - perpDir * (arrowSize/2);
            
            QPolygonF arrow;
            arrow << QPointF(end[0], end[1])
                  << QPointF(arrowPt1[0], arrowPt1[1])
                  << QPointF(arrowPt2[0], arrowPt2[1]);
            
            QGraphicsPolygonItem* arrowHead = new QGraphicsPolygonItem(arrow);
            arrowHead->setBrush(QBrush(lineColor));
            arrowHead->setPen(Qt::NoPen);
            arrowHead->setZValue(zOrder_);
            
            items.push_back(arrowHead);
        }
    }
    
    return items;
}

// COverlayCurve implementation
COverlayCurve::COverlayCurve(const QString& id)
    : COverlayItem(CURVE, id)
{
}

void COverlayCurve::setCurvePoints(const std::vector<cv::Vec3f>& points)
{
    volumeCoords_ = points;
}

void COverlayCurve::addCurvePoint(const cv::Vec3f& point)
{
    volumeCoords_.push_back(point);
}

void COverlayCurve::clearCurvePoints()
{
    volumeCoords_.clear();
}

std::vector<QGraphicsItem*> COverlayCurve::createGraphicsItems(
    const std::vector<cv::Vec2f>& sceneCoords) const
{
    std::vector<QGraphicsItem*> items;
    
    if (sceneCoords.size() < 2) {
        return items;
    }
    
    QPainterPath path;
    
    if (isSmooth_) {
        // Create smooth curve using quadratic Bezier curves
        path.moveTo(sceneCoords[0][0], sceneCoords[0][1]);
        
        for (size_t i = 1; i < sceneCoords.size(); ++i) {
            const cv::Vec2f& prevPt = sceneCoords[i-1];
            const cv::Vec2f& currPt = sceneCoords[i];
            
            // Control point is midpoint
            cv::Vec2f controlPt = (prevPt + currPt) * 0.5f;
            
            path.quadTo(controlPt[0], controlPt[1],
                       currPt[0], currPt[1]);
        }
    } else {
        // Create polyline
        path.moveTo(sceneCoords[0][0], sceneCoords[0][1]);
        
        for (size_t i = 1; i < sceneCoords.size(); ++i) {
            path.lineTo(sceneCoords[i][0], sceneCoords[i][1]);
        }
    }
    
    if (isClosed_) {
        path.closeSubpath();
    }
    
    // Create the path item
    QGraphicsPathItem* curveItem = new QGraphicsPathItem(path);
    
    // Set pen with color and style
    QColor curveColor = color_;
    curveColor.setAlphaF(opacity_);
    QPen pen(curveColor, lineWidth_, Qt::SolidLine);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(lineJoinStyle_);
    curveItem->setPen(pen);
    
    // If closed, optionally fill
    if (isClosed_) {
        QColor fillColor = color_;
        fillColor.setAlphaF(opacity_ * 0.3f); // Semi-transparent fill
        curveItem->setBrush(QBrush(fillColor));
    }
    
    curveItem->setZValue(zOrder_);
    items.push_back(curveItem);
    
    return items;
}

} // namespace ChaoVis
