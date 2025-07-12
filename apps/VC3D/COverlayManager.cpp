#include "COverlayManager.hpp"
#include "vc/core/util/Surface.hpp"
#include <QGraphicsScene>
#include <iostream>

namespace ChaoVis {

COverlayManager::COverlayManager(QObject* parent)
    : QObject(parent)
{
}

COverlayManager::~COverlayManager()
{
    // Clean up any remaining rendered items
    for (auto& pair : renderedItems_) {
        for (auto* item : pair.second) {
            delete item;
        }
    }
}

bool COverlayManager::addOverlay(std::shared_ptr<COverlayItem> item)
{
    if (!item) {
        return false;
    }
    
    const QString& id = item->getId();
    
    // Check if ID already exists
    if (overlays_.find(id) != overlays_.end()) {
        return false;
    }
    
    overlays_[id] = item;
    emit overlayAdded(id);
    emit overlaysChanged();
    
    return true;
}

bool COverlayManager::removeOverlay(const QString& id)
{
    auto it = overlays_.find(id);
    if (it == overlays_.end()) {
        return false;
    }
    
    // Clean up rendered items for this overlay
    auto renderedIt = renderedItems_.find(id);
    if (renderedIt != renderedItems_.end()) {
        for (auto* item : renderedIt->second) {
            if (item->scene()) {
                item->scene()->removeItem(item);
            }
            delete item;
        }
        renderedItems_.erase(renderedIt);
    }
    
    overlays_.erase(it);
    emit overlayRemoved(id);
    emit overlaysChanged();
    
    return true;
}

void COverlayManager::clearOverlays()
{
    // Clean up all rendered items
    for (auto& pair : renderedItems_) {
        for (auto* item : pair.second) {
            if (item->scene()) {
                item->scene()->removeItem(item);
            }
            delete item;
        }
    }
    renderedItems_.clear();
    
    overlays_.clear();
    emit overlaysChanged();
}

std::shared_ptr<COverlayItem> COverlayManager::getOverlay(const QString& id) const
{
    auto it = overlays_.find(id);
    if (it != overlays_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<QString> COverlayManager::getOverlayIds() const
{
    std::vector<QString> ids;
    ids.reserve(overlays_.size());
    
    for (const auto& pair : overlays_) {
        ids.push_back(pair.first);
    }
    
    return ids;
}

std::vector<std::shared_ptr<COverlayItem>> COverlayManager::getOverlaysByType(
    COverlayItem::Type type) const
{
    std::vector<std::shared_ptr<COverlayItem>> result;
    
    for (const auto& pair : overlays_) {
        if (pair.second->getType() == type) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

cv::Vec3f COverlayManager::sceneToVolume(const QPointF& scenePoint,
                                        Surface* surface,
                                        const cv::Vec2f& visCenter,
                                        float scale) const
{
    if (!surface) {
        return cv::Vec3f(0, 0, 0);
    }
    
    // Check if it's a PlaneSurface
    if (PlaneSurface* plane = dynamic_cast<PlaneSurface*>(surface)) {
        cv::Vec3f surfLoc = {static_cast<float>(scenePoint.x())/scale, 
                            static_cast<float>(scenePoint.y())/scale, 
                            0};
        SurfacePointer* ptr = plane->pointer();
        return plane->coord(ptr, surfLoc);
    }
    // Check if it's a QuadSurface (e.g., segmentation)
    else if (QuadSurface* quad = dynamic_cast<QuadSurface*>(surface)) {
        cv::Vec3f delta = {static_cast<float>(scenePoint.x() - visCenter[0])/scale,
                          static_cast<float>(scenePoint.y() - visCenter[1])/scale,
                          0};
        SurfacePointer* ptr = quad->pointer();
        return quad->coord(ptr, delta);
    }
    
    return cv::Vec3f(0, 0, 0);
}

cv::Vec2f COverlayManager::volumeToScene(const cv::Vec3f& volumePoint,
                                        Surface* surface,
                                        const cv::Vec2f& visCenter,
                                        float scale,
                                        float maxDistance) const
{
    if (!surface) {
        return cv::Vec2f(-1, -1);
    }
    
    // Check if it's a PlaneSurface
    if (PlaneSurface* plane = dynamic_cast<PlaneSurface*>(surface)) {
        float dist = plane->pointDist(volumePoint);
        if (dist >= maxDistance) {
            return cv::Vec2f(-1, -1);
        }
        cv::Vec3f projected = plane->project(volumePoint, 1.0, scale);
        return cv::Vec2f(projected[0], projected[1]);
    }
    // Check if it's a QuadSurface
    else if (QuadSurface* quad = dynamic_cast<QuadSurface*>(surface)) {
        SurfacePointer* ptr = quad->pointer();
        float res = quad->pointTo(ptr, volumePoint, maxDistance, 100);
        if (res >= maxDistance) {
            return cv::Vec2f(-1, -1);
        }
        cv::Vec3f loc = quad->loc(ptr) * scale;
        return cv::Vec2f(loc[0] + visCenter[0], loc[1] + visCenter[1]);
    }
    
    return cv::Vec2f(-1, -1);
}

cv::Vec3f COverlayManager::projectToSurface(const cv::Vec3f& point,
                                           Surface* surface) const
{
    if (!surface) {
        return point;
    }
    
    // For PlaneSurface, project the point onto the plane
    if (PlaneSurface* plane = dynamic_cast<PlaneSurface*>(surface)) {
        cv::Vec3f projected = plane->project(point, 1.0, 1.0);
        // Convert back to volume coordinates
        SurfacePointer* ptr = plane->pointer();
        return plane->coord(ptr, projected);
    }
    // For QuadSurface, find the closest point on the surface
    else if (QuadSurface* quad = dynamic_cast<QuadSurface*>(surface)) {
        SurfacePointer* ptr = quad->pointer();
        float res = quad->pointTo(ptr, point, 10.0, 1000);
        if (res < 10.0) {
            return quad->coord(ptr);
        }
    }
    
    return point;
}

void COverlayManager::renderOverlays(QGraphicsScene* scene,
                                    Surface* surface,
                                    const cv::Vec2f& visCenter,
                                    float scale,
                                    float zOffset)
{
    if (!scene || !surface) {
        return;
    }
    
    // Clear existing rendered items
    clearRenderedItems(scene);
    
    // Render each visible overlay
    for (const auto& pair : overlays_) {
        const QString& id = pair.first;
        const auto& overlay = pair.second;
        
        if (!overlay->isVisible()) {
            continue;
        }
        
        // Transform volume coordinates to scene coordinates
        std::vector<cv::Vec2f> sceneCoords = transformToScene(
            overlay->getVolumeCoords(), surface, visCenter, scale);
        
        // Skip if no valid coordinates
        if (sceneCoords.empty()) {
            continue;
        }
        
        // Create graphics items for this overlay
        std::vector<QGraphicsItem*> items = overlay->createGraphicsItems(sceneCoords);
        
        // Add items to scene and track them
        for (auto* item : items) {
            scene->addItem(item);
        }
        
        renderedItems_[id] = std::move(items);
    }
}

void COverlayManager::clearRenderedItems(QGraphicsScene* scene)
{
    for (auto& pair : renderedItems_) {
        for (auto* item : pair.second) {
            if (scene && item->scene() == scene) {
                scene->removeItem(item);
            }
            delete item;
        }
    }
    renderedItems_.clear();
}

void COverlayManager::setAllOverlaysVisible(bool visible)
{
    for (auto& pair : overlays_) {
        pair.second->setVisible(visible);
    }
    emit overlaysChanged();
}

bool COverlayManager::hasOverlaysInRegion(const cv::Vec3f& minPt,
                                         const cv::Vec3f& maxPt) const
{
    for (const auto& pair : overlays_) {
        const auto& overlay = pair.second;
        auto bbox = overlay->getBoundingBox();
        
        // Check if bounding boxes intersect
        bool intersects = !(bbox.first[0] > maxPt[0] || bbox.second[0] < minPt[0] ||
                           bbox.first[1] > maxPt[1] || bbox.second[1] < minPt[1] ||
                           bbox.first[2] > maxPt[2] || bbox.second[2] < minPt[2]);
        
        if (intersects) {
            return true;
        }
    }
    
    return false;
}

std::vector<cv::Vec2f> COverlayManager::transformToScene(
    const std::vector<cv::Vec3f>& volumeCoords,
    Surface* surface,
    const cv::Vec2f& visCenter,
    float scale,
    float maxDistance) const
{
    std::vector<cv::Vec2f> sceneCoords;
    sceneCoords.reserve(volumeCoords.size());
    
    for (const auto& volCoord : volumeCoords) {
        cv::Vec2f sceneCoord = volumeToScene(volCoord, surface, visCenter, scale, maxDistance);
        
        // Only include valid coordinates
        if (sceneCoord[0] != -1) {
            sceneCoords.push_back(sceneCoord);
        }
    }
    
    return sceneCoords;
}

} // namespace ChaoVis
