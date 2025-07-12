#pragma once

#include <memory>
#include <vector>
#include <QtWidgets>
#include <opencv2/core/core.hpp>

namespace ChaoVis {

/**
 * @brief Base class for all overlay items in the volume viewer
 * 
 * This class provides the common interface and properties for all types of
 * overlays that can be rendered on the volume views. Derived classes implement
 * specific overlay types like points, lines, and curves.
 */
class COverlayItem
{
public:
    /**
     * @brief Types of overlay items
     */
    enum Type {
        POINT,      ///< Single point overlay
        LINE,       ///< Line segment overlay  
        CURVE,      ///< Curve/path overlay
        POLYGON,    ///< Closed polygon overlay
        CUSTOM      ///< Custom overlay type
    };
    
    /**
     * @brief Construct a new overlay item
     * @param type The type of overlay
     * @param id Unique identifier for this overlay
     */
    COverlayItem(Type type, const QString& id);
    
    virtual ~COverlayItem() = default;
    
    // Getters
    Type getType() const { return type_; }
    const QString& getId() const { return id_; }
    const std::vector<cv::Vec3f>& getVolumeCoords() const { return volumeCoords_; }
    const QColor& getColor() const { return color_; }
    float getOpacity() const { return opacity_; }
    bool isVisible() const { return visible_; }
    float getLineWidth() const { return lineWidth_; }
    int getZOrder() const { return zOrder_; }
    
    // Setters
    void setVolumeCoords(const std::vector<cv::Vec3f>& coords) { volumeCoords_ = coords; }
    void addVolumeCoord(const cv::Vec3f& coord) { volumeCoords_.push_back(coord); }
    void setColor(const QColor& color) { color_ = color; }
    void setOpacity(float opacity) { opacity_ = std::clamp(opacity, 0.0f, 1.0f); }
    void setVisible(bool visible) { visible_ = visible; }
    void setLineWidth(float width) { lineWidth_ = width; }
    void setZOrder(int order) { zOrder_ = order; }
    
    /**
     * @brief Create graphics items for rendering this overlay
     * @param scene The graphics scene to add items to
     * @param sceneCoords The coordinates transformed to scene space
     * @return Vector of created graphics items
     */
    virtual std::vector<QGraphicsItem*> createGraphicsItems(
        const std::vector<cv::Vec2f>& sceneCoords) const = 0;
    
    /**
     * @brief Check if a point is valid for this overlay type
     * @param coord The coordinate to check
     * @return true if the coordinate is valid
     */
    virtual bool isValidCoordinate(const cv::Vec3f& coord) const { return true; }
    
    /**
     * @brief Get the bounding box of this overlay in volume coordinates
     * @return Bounding box as pair of min and max points
     */
    virtual std::pair<cv::Vec3f, cv::Vec3f> getBoundingBox() const;
    
protected:
    Type type_;                              ///< Type of overlay
    QString id_;                             ///< Unique identifier
    std::vector<cv::Vec3f> volumeCoords_;    ///< 3D coordinates in volume space
    QColor color_ = Qt::yellow;              ///< Overlay color
    float opacity_ = 1.0f;                   ///< Opacity (0.0 - 1.0)
    bool visible_ = true;                    ///< Visibility flag
    float lineWidth_ = 2.0f;                 ///< Line width for rendering
    int zOrder_ = 50;                        ///< Z-order for layering
};

/**
 * @brief Point overlay implementation
 */
class COverlayPoint : public COverlayItem
{
public:
    COverlayPoint(const QString& id);
    
    /**
     * @brief Set the single point coordinate
     * @param coord The 3D volume coordinate
     */
    void setPoint(const cv::Vec3f& coord);
    
    /**
     * @brief Get the point coordinate
     * @return The 3D volume coordinate
     */
    cv::Vec3f getPoint() const;
    
    // Point-specific properties
    void setPointSize(float size) { pointSize_ = size; }
    float getPointSize() const { return pointSize_; }
    
    void setPointShape(Qt::PenCapStyle shape) { pointShape_ = shape; }
    Qt::PenCapStyle getPointShape() const { return pointShape_; }
    
    std::vector<QGraphicsItem*> createGraphicsItems(
        const std::vector<cv::Vec2f>& sceneCoords) const override;
    
private:
    float pointSize_ = 8.0f;                 ///< Point diameter
    Qt::PenCapStyle pointShape_ = Qt::RoundCap;  ///< Point shape
};

/**
 * @brief Line overlay implementation
 */
class COverlayLine : public COverlayItem
{
public:
    COverlayLine(const QString& id);
    
    /**
     * @brief Set the line endpoints
     * @param start Start point in volume coordinates
     * @param end End point in volume coordinates
     */
    void setLine(const cv::Vec3f& start, const cv::Vec3f& end);
    
    /**
     * @brief Get line endpoints
     * @return Pair of start and end points
     */
    std::pair<cv::Vec3f, cv::Vec3f> getLine() const;
    
    // Line-specific properties
    void setLineStyle(Qt::PenStyle style) { lineStyle_ = style; }
    Qt::PenStyle getLineStyle() const { return lineStyle_; }
    
    void setArrowHead(bool enabled) { hasArrowHead_ = enabled; }
    bool hasArrowHead() const { return hasArrowHead_; }
    
    std::vector<QGraphicsItem*> createGraphicsItems(
        const std::vector<cv::Vec2f>& sceneCoords) const override;
    
private:
    Qt::PenStyle lineStyle_ = Qt::SolidLine;  ///< Line style
    bool hasArrowHead_ = false;               ///< Draw arrow head at end
};

/**
 * @brief Curve overlay implementation
 */
class COverlayCurve : public COverlayItem
{
public:
    COverlayCurve(const QString& id);
    
    /**
     * @brief Set all curve points at once
     * @param points Vector of 3D volume coordinates
     */
    void setCurvePoints(const std::vector<cv::Vec3f>& points);
    
    /**
     * @brief Add a single point to the curve
     * @param point 3D volume coordinate to add
     */
    void addCurvePoint(const cv::Vec3f& point);
    
    /**
     * @brief Clear all curve points
     */
    void clearCurvePoints();
    
    // Curve-specific properties
    void setClosed(bool closed) { isClosed_ = closed; }
    bool isClosed() const { return isClosed_; }
    
    void setSmooth(bool smooth) { isSmooth_ = smooth; }
    bool isSmooth() const { return isSmooth_; }
    
    void setLineJoinStyle(Qt::PenJoinStyle style) { lineJoinStyle_ = style; }
    Qt::PenJoinStyle getLineJoinStyle() const { return lineJoinStyle_; }
    
    std::vector<QGraphicsItem*> createGraphicsItems(
        const std::vector<cv::Vec2f>& sceneCoords) const override;
    
private:
    bool isClosed_ = false;                      ///< Whether curve forms closed loop
    bool isSmooth_ = false;                      ///< Use smooth interpolation
    Qt::PenJoinStyle lineJoinStyle_ = Qt::RoundJoin;  ///< Line join style
};

} // namespace ChaoVis
