#pragma once

#include <QObject>
#include <memory>
#include <map>
#include <vector>
#include "COverlayItem.hpp"

class Surface;
class PlaneSurface;
class QuadSurface;
class QGraphicsScene;

namespace ChaoVis {

// Forward declaration inside namespace
class CSurfaceCollection;

/**
 * @brief Manager class for overlay items in the volume viewer
 * 
 * This class manages a collection of overlay items and handles their
 * rendering, coordinate transformation, and interaction with surfaces.
 */
class COverlayManager : public QObject
{
    Q_OBJECT
    
public:
    /**
     * @brief Construct a new overlay manager
     * @param parent Parent QObject
     */
    explicit COverlayManager(QObject* parent = nullptr);
    
    ~COverlayManager();
    
    /**
     * @brief Add an overlay item to the manager
     * @param item The overlay item to add
     * @return true if successfully added, false if ID already exists
     */
    bool addOverlay(std::shared_ptr<COverlayItem> item);
    
    /**
     * @brief Remove an overlay by ID
     * @param id The ID of the overlay to remove
     * @return true if removed, false if not found
     */
    bool removeOverlay(const QString& id);
    
    /**
     * @brief Remove all overlays
     */
    void clearOverlays();
    
    /**
     * @brief Get an overlay by ID
     * @param id The ID to look up
     * @return Pointer to overlay or nullptr if not found
     */
    std::shared_ptr<COverlayItem> getOverlay(const QString& id) const;
    
    /**
     * @brief Get all overlay IDs
     * @return Vector of overlay IDs
     */
    std::vector<QString> getOverlayIds() const;
    
    /**
     * @brief Get all overlays of a specific type
     * @param type The overlay type to filter by
     * @return Vector of overlays matching the type
     */
    std::vector<std::shared_ptr<COverlayItem>> getOverlaysByType(
        COverlayItem::Type type) const;
    
    /**
     * @brief Transform scene coordinates to volume coordinates
     * @param scenePoint Point in scene coordinates
     * @param surface The surface to use for transformation
     * @param visCenter The visible center in scene coordinates
     * @param scale The current scale factor
     * @return Point in volume coordinates
     */
    cv::Vec3f sceneToVolume(const QPointF& scenePoint,
                           Surface* surface,
                           const cv::Vec2f& visCenter,
                           float scale) const;
    
    /**
     * @brief Transform volume coordinates to scene coordinates
     * @param volumePoint Point in volume coordinates
     * @param surface The surface to use for transformation
     * @param visCenter The visible center in scene coordinates
     * @param scale The current scale factor
     * @param maxDistance Maximum distance from surface (for visibility)
     * @return Point in scene coordinates, or (-1,-1) if not visible
     */
    cv::Vec2f volumeToScene(const cv::Vec3f& volumePoint,
                           Surface* surface,
                           const cv::Vec2f& visCenter,
                           float scale,
                           float maxDistance = 4.0f) const;
    
    /**
     * @brief Project a point onto a surface
     * @param point The point to project
     * @param surface The surface to project onto
     * @return The projected point on the surface
     */
    cv::Vec3f projectToSurface(const cv::Vec3f& point,
                              Surface* surface) const;
    
    /**
     * @brief Render all visible overlays to the scene
     * @param scene The graphics scene to render to
     * @param surface The current surface for coordinate transformation
     * @param visCenter The visible center in scene coordinates
     * @param scale The current scale factor
     * @param zOffset Optional Z offset for slicing
     */
    void renderOverlays(QGraphicsScene* scene,
                       Surface* surface,
                       const cv::Vec2f& visCenter,
                       float scale,
                       float zOffset = 0.0f);
    
    /**
     * @brief Clear all rendered items from the scene
     * @param scene The graphics scene to clear from
     */
    void clearRenderedItems(QGraphicsScene* scene);
    
    /**
     * @brief Set visibility for all overlays
     * @param visible The visibility state
     */
    void setAllOverlaysVisible(bool visible);
    
    /**
     * @brief Check if any overlays are within the given bounding box
     * @param minPt Minimum point of bounding box
     * @param maxPt Maximum point of bounding box
     * @return true if any overlays intersect the box
     */
    bool hasOverlaysInRegion(const cv::Vec3f& minPt,
                            const cv::Vec3f& maxPt) const;

signals:
    /**
     * @brief Emitted when an overlay is added
     * @param id ID of the added overlay
     */
    void overlayAdded(const QString& id);
    
    /**
     * @brief Emitted when an overlay is removed
     * @param id ID of the removed overlay
     */
    void overlayRemoved(const QString& id);
    
    /**
     * @brief Emitted when an overlay is modified
     * @param id ID of the modified overlay
     */
    void overlayModified(const QString& id);
    
    /**
     * @brief Emitted when overlays need to be re-rendered
     */
    void overlaysChanged();
    
private:
    // Storage for overlay items
    std::map<QString, std::shared_ptr<COverlayItem>> overlays_;
    
    // Track rendered items for cleanup
    std::map<QString, std::vector<QGraphicsItem*>> renderedItems_;
    
    // Helper to transform a vector of volume coords to scene coords
    std::vector<cv::Vec2f> transformToScene(
        const std::vector<cv::Vec3f>& volumeCoords,
        Surface* surface,
        const cv::Vec2f& visCenter,
        float scale,
        float maxDistance = 4.0f) const;
};

} // namespace ChaoVis
