// CVolumeViewer.cpp
// Chao Du 2015 April
#include "CVolumeViewer.hpp"
#include "UDataManipulateUtils.hpp"
#include "HBase.hpp"

#include <QGraphicsView>
#include <QGraphicsScene>

#include "CVolumeViewerView.hpp"
#include "CSurfaceCollection.hpp"
#include "VCCollection.hpp"
#include "COutlinedTextItem.hpp"

#include "vc/core/types/VolumePkg.hpp"
#include "vc/core/util/Surface.hpp"
#include "vc/core/util/Slicing.hpp"

#include <omp.h>

#include "OpChain.hpp"

using namespace ChaoVis;
using qga = QGuiApplication;

#define BGND_RECT_MARGIN 8
#define DEFAULT_TEXT_COLOR QColor(255, 255, 120)
// More gentle zoom factor for smoother experience
#define ZOOM_FACTOR 1.05 // Reduced from 1.15 for even smoother zooming

#define COLOR_CURSOR Qt::cyan
#define COLOR_FOCUS QColor(50, 255, 215)
#define COLOR_SEG_YZ Qt::yellow
#define COLOR_SEG_XZ Qt::red
#define COLOR_SEG_XY QColor(255, 140, 0)

CVolumeViewer::CVolumeViewer(CSurfaceCollection *col, QWidget* parent)
    : QWidget(parent)
    , fGraphicsView(nullptr)
    , fBaseImageItem(nullptr)
    , _surf_col(col)
    , _highlighted_point_id(0)
    , _selected_point_id(0)
    , _dragged_point_id(0)
{
    // Create graphics view
    fGraphicsView = new CVolumeViewerView(this);
    
    fGraphicsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    fGraphicsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    
    fGraphicsView->setTransformationAnchor(QGraphicsView::NoAnchor);
    
    fGraphicsView->setRenderHint(QPainter::Antialiasing);
    // setFocusProxy(fGraphicsView);
    connect(fGraphicsView, &CVolumeViewerView::sendScrolled, this, &CVolumeViewer::onScrolled);
    connect(fGraphicsView, &CVolumeViewerView::sendVolumeClicked, this, &CVolumeViewer::onVolumeClicked);
    connect(fGraphicsView, &CVolumeViewerView::sendZoom, this, &CVolumeViewer::onZoom);
    connect(fGraphicsView, &CVolumeViewerView::sendCursorMove, this, &CVolumeViewer::onCursorMove);
    connect(fGraphicsView, &CVolumeViewerView::sendPanRelease, this, &CVolumeViewer::onPanRelease);
    connect(fGraphicsView, &CVolumeViewerView::sendPanStart, this, &CVolumeViewer::onPanStart);
    connect(fGraphicsView, &CVolumeViewerView::sendMousePress, this, &CVolumeViewer::onMousePress);
    connect(fGraphicsView, &CVolumeViewerView::sendMouseMove, this, &CVolumeViewer::onMouseMove);
    connect(fGraphicsView, &CVolumeViewerView::sendMouseRelease, this, &CVolumeViewer::onMouseRelease);
    connect(fGraphicsView, &CVolumeViewerView::sendKeyRelease, this, &CVolumeViewer::onKeyRelease);

    // Create graphics scene
    fScene = new QGraphicsScene({-2500,-2500,5000,5000}, this);

    // Set the scene
    fGraphicsView->setScene(fScene);

    QSettings settings("VC.ini", QSettings::IniFormat);
    // fCenterOnZoomEnabled = settings.value("viewer/center_on_zoom", false).toInt() != 0;
    // fScrollSpeed = settings.value("viewer/scroll_speed", false).toInt();
    fSkipImageFormatConv = settings.value("perf/chkSkipImageFormatConvExp", false).toBool();
    _downscale_override = settings.value("perf/downscale_override", 0).toInt();

    QVBoxLayout* aWidgetLayout = new QVBoxLayout;
    aWidgetLayout->addWidget(fGraphicsView);

    setLayout(aWidgetLayout);

    _deferredUpdateTimer = new QTimer(this);
    _deferredUpdateTimer->setSingleShot(true);
    _deferredUpdateTimer->setInterval(50); // 150ms delay after last interaction
    connect(_deferredUpdateTimer, &QTimer::timeout, this, &CVolumeViewer::performDeferredUpdates);


    _lbl = new QLabel(this);
    _lbl->setStyleSheet("QLabel { color : white; }");
    _lbl->move(10,5);
}

// Destructor
CVolumeViewer::~CVolumeViewer(void)
{
    deleteNULL(fGraphicsView);
    deleteNULL(fScene);
}

void round_scale(float &scale)
{
    if (abs(scale-round(log2(scale))) < 0.02f)
        scale = pow(2,round(log2(scale)));
    // the most reduced OME zarr projection is 32x so make the min zoom out 1/32 = 0.03125
    if (scale < 0.03125f) scale = 0.03125f;
    if (scale > 4.0f) scale = 4.0f;
}

//get center of current visible area in scene coordinates
QPointF visible_center(QGraphicsView *view)
{
    QRectF bbox = view->mapToScene(view->viewport()->geometry()).boundingRect();
    return bbox.topLeft() + QPointF(bbox.width(),bbox.height())*0.5;
}


QPointF CVolumeViewer::volumeToScene(const cv::Vec3f& vol_point)
{
    PlaneSurface* plane = dynamic_cast<PlaneSurface*>(_surf);
    QuadSurface* quad = dynamic_cast<QuadSurface*>(_surf);
    cv::Vec3f p;

    if (plane) {
        p = plane->project(vol_point, 1.0, _scale);
    } else if (quad) {
        SurfacePointer* ptr = quad->pointer();
        _surf->pointTo(ptr, vol_point, 4.0, 100);
        p = _surf->loc(ptr) * _scale;
    }

    return QPointF(p[0], p[1]);
}

bool scene2vol(cv::Vec3f &p, cv::Vec3f &n, Surface *_surf, const std::string &_surf_name, CSurfaceCollection *_surf_col, const QPointF &scene_loc, const cv::Vec2f &_vis_center, float _ds_scale)
{
    // Safety check for null surface
    if (!_surf) {
        p = cv::Vec3f(0, 0, 0);
        n = cv::Vec3f(0, 0, 1);
        return false;
    }
    
    try {
        cv::Vec3f surf_loc = {scene_loc.x()/_ds_scale, scene_loc.y()/_ds_scale,0};
        
        SurfacePointer *ptr = _surf->pointer();
        
        n = _surf->normal(ptr, surf_loc);
        p = _surf->coord(ptr, surf_loc);
    } catch (const cv::Exception& e) {
        return false;
    }
    return true;
}

void CVolumeViewer::onCursorMove(QPointF scene_loc)
{
    if (!_surf || !_surf_col)
        return;

    cv::Vec3f p, n;
    if (!scene2vol(p, n, _surf, _surf_name, _surf_col, scene_loc, _vis_center, _scale)) {
        if (_cursor) _cursor->hide();
        return;
    }
    if (_cursor) _cursor->show();

    // Standard cursor update
    POI *cursor = _surf_col->poi("cursor");
    if (!cursor)
        cursor = new POI;
    
    cursor->p = p;
    
    _surf_col->setPOI("cursor", cursor);

    // Point highlighting logic
    if (!_point_collection) return;

    uint64_t old_highlighted_id = _highlighted_point_id;
    _highlighted_point_id = 0;

    if (_dragged_point_id == 0) { // Only highlight if not dragging
        const float highlight_dist_threshold = 10.0f; // pixels
        float min_dist_sq = highlight_dist_threshold * highlight_dist_threshold;

        for (const auto& item_pair : _points_items) {
            auto item = item_pair.second.circle;
            QPointF point_scene_pos = item->rect().center();
            QPointF diff = scene_loc - point_scene_pos;
            float dist_sq = QPointF::dotProduct(diff, diff);
            if (dist_sq < min_dist_sq) {
                min_dist_sq = dist_sq;
                _highlighted_point_id = item_pair.first;
            }
        }
    }

    if (old_highlighted_id != _highlighted_point_id) {
        if (auto old_point = _point_collection->getPoint(old_highlighted_id)) {
            renderOrUpdatePoint(*old_point);
        }
        if (auto new_point = _point_collection->getPoint(_highlighted_point_id)) {
            renderOrUpdatePoint(*new_point);
        }
    }
}

void CVolumeViewer::recalcScales()
{
    float old_ds = _ds_scale;         // remember previous level
    // if (dynamic_cast<PlaneSurface*>(_surf))
    _min_scale = pow(2.0,1.-volume->numScales());
    // else
        // _min_scale = std::max(pow(2.0,1.-volume->numScales()), 0.5);
    
    /* -------- chooses _ds_scale/_ds_sd_idx -------- */
    if      (_scale >= _max_scale) { _ds_sd_idx = 0;                         }
    else if (_scale <  _min_scale) { _ds_sd_idx = volume->numScales()-1;     }
    else  { _ds_sd_idx = int(std::round(-std::log2(_scale))); }
    if (_downscale_override > 0) {
        _ds_sd_idx += _downscale_override;
        // Clamp to available scales
        _ds_sd_idx = std::min(_ds_sd_idx, (int)volume->numScales() - 1);
    }
    _ds_scale = std::pow(2.0f, -_ds_sd_idx);
    /* ---------------------------------------------------------------- */

    /* ---- refresh physical voxel size when pyramid level flips -- */
    if (volume && std::abs(_ds_scale - old_ds) > 1e-6f)
    {
        double vs = volume->voxelSize() / _ds_scale;   // µm per scene-unit
        fGraphicsView->setVoxelSize(vs, vs);           // keep scalebar honest
    }
}

void CVolumeViewer::onZoom(int steps, QPointF scene_loc, Qt::KeyboardModifiers modifiers)
{
    // Defer expensive invalidations
    _deferredInvalidateVis = true;
    _deferredInvalidateIntersect = true;
    _deferredUpdateTimer->stop();
    _deferredUpdateTimer->start();

    for(auto &col : _intersect_items)
        for(auto &item : col.second)
            item->setVisible(false);

    if (!_surf)
        return;

    if (modifiers & Qt::ShiftModifier) {
        // Z slice navigation with shift+scroll
        int adjustedSteps = steps;

        // Use single z step for segmentation surface
        if (_surf_name == "segmentation") {
            adjustedSteps = (steps > 0) ? 1 : -1;  // Always step by 1 slice regardless of wheel delta
        }

        _z_off += adjustedSteps;

        // Update the focus POI Z position
        POI *poi = _surf_col->poi("focus");
        if (poi && volume) {
            // Calculate the new Z value
            int newZ = static_cast<int>(poi->p[2] + adjustedSteps);
            // Make sure it's within bounds
            newZ = std::max(0, std::min(newZ, static_cast<int>(volume->numSlices() - 1)));

            // Update POI z position
            poi->p[2] = newZ;
            _surf_col->setPOI("focus", poi);

            // Emit signal for Z slice change
            emit sendZSliceChanged(newZ);
        }

        renderVisible(true);
    }
    else {
        float zoom = pow(ZOOM_FACTOR, steps);

        // update the scale used when projecting voxels to scene space
        _scale *= zoom;
        round_scale(_scale);
        recalcScales();

        // Cache the view-space mouse position; used later for cursor update
        QPoint pointViewportBefore = fGraphicsView->mapFromScene(scene_loc);

        // The above scale is *not* part of Qt's scene-to-view transform, but part of the voxel-to-scene transform
        // implemented in PlaneSurface::project; it causes a zoom around the surface origin
        // Translations are represented in the Qt scene-to-view transform; these move the surface origin within the viewpoint
        // To zoom centered on the mouse, we adjust the scene-to-view translation appropriately
        // If the mouse were at the plane/surface origin, this adjustment should be zero
        // If the mouse were right of the plane origin, should translate to the left so that point ends up where it was
        fGraphicsView->translate(scene_loc.x() * (1 - zoom), scene_loc.y() * (1 - zoom));

        // Update the cursor (which lives in scene space) to still lie under the mouse even after the above translation
        QPointF pointSceneAfter = fGraphicsView->mapToScene(pointViewportBefore);
        onCursorMove(pointSceneAfter);

        curr_img_area = {0,0,0,0};
        // Update scene rect
        //FIXME get correct size for slice!
        int max_size = 100000; //std::max(volume->sliceWidth(), std::max(volume->numSlices(), volume->sliceHeight()))*_ds_scale + 512;
        fGraphicsView->setSceneRect(-max_size/2, -max_size/2, max_size, max_size);
        renderVisible(); // Keep immediate render for smooth interaction

        // Re-render all points to update their positions according to the new scale
        refreshPointPositions();
    }

    _lbl->setText(QString("%1x %2").arg(_scale).arg(_z_off));

    // Defer rendering intersections
    _deferredRenderIntersections = true;
}

void CVolumeViewer::OnVolumeChanged(volcart::Volume::Pointer volume_)
{
    volume = volume_;
    
    // printf("sizes %d %d %d\n", volume_->sliceWidth(), volume_->sliceHeight(), volume_->numSlices());

    int max_size = 100000 ;//std::max(volume_->sliceWidth(), std::max(volume_->numSlices(), volume_->sliceHeight()))*_ds_scale + 512;
    // printf("max size %d\n", max_size);
    fGraphicsView->setSceneRect(-max_size/2,-max_size/2,max_size,max_size);
    
    if (volume->numScales() >= 2) {
        //FIXME currently hardcoded
        _max_scale = 0.5;
        _min_scale = pow(2.0,1.-volume->numScales());
    }
    else {
        //FIXME currently hardcoded
        _max_scale = 1.0;
        _min_scale = 1.0;
    }
    
    recalcScales();

    _lbl->setText(QString("%1x %2").arg(_scale).arg(_z_off));

    renderVisible(true);

    // ——— Scalebar: physical size per scene-unit, compensating for down-sampling ———
    // volume->voxelSize() is µm per original voxel;
    // each scene-unit is still one original voxel, but we read data at (_ds_scale) resolution,
    // so we scale the voxelSize by 1/_ds_scale.
    double vs = volume->voxelSize() / _ds_scale;
    fGraphicsView->setVoxelSize(vs, vs);
}

void CVolumeViewer::onVolumeClicked(QPointF scene_loc, Qt::MouseButton buttons, Qt::KeyboardModifiers modifiers)
{
    if (!_surf)
        return;

    // If a point was being dragged, don't do anything on release
    if (_dragged_point_id != 0) {
        return;
    }

    cv::Vec3f p, n;
    if (!scene2vol(p, n, _surf, _surf_name, _surf_col, scene_loc, _vis_center, _scale))
        return;

    if (buttons == Qt::LeftButton) {
        bool isShift = modifiers.testFlag(Qt::ShiftModifier);

        if (isShift) {
            // If a collection is selected, add to it.
            if (_selected_collection_id != 0) {
                const auto& collections = _point_collection->getAllCollections();
                auto it = collections.find(_selected_collection_id);
                if (it != collections.end()) {
                    _point_collection->addPoint(it->second.name, p);
                }
            } else {
                // Otherwise, create a new collection.
                std::string new_name = _point_collection->generateNewCollectionName("col");
                auto new_point = _point_collection->addPoint(new_name, p);
                _selected_collection_id = new_point.collectionId;
                emit sendCollectionSelected(_selected_collection_id);
            }
        }
    }

    // Forward the click for focus
    if (dynamic_cast<PlaneSurface*>(_surf))
        sendVolumeClicked(p, n, _surf, buttons, modifiers);
    else if (_surf_name == "segmentation")
        sendVolumeClicked(p, n, _surf_col->surface("segmentation"), buttons, modifiers);
    else
        std::cout << "FIXME: onVolumeClicked()" << std::endl;
}

void CVolumeViewer::setCache(ChunkCache *cache_)
{
    cache = cache_;
}

void CVolumeViewer::setPointCollection(VCCollection* point_collection)
{
    if (_point_collection) {
        disconnect(_point_collection, &VCCollection::collectionChanged, this, &CVolumeViewer::onCollectionChanged);
    }
    _point_collection = point_collection;
    if (_point_collection) {
        connect(_point_collection, &VCCollection::collectionChanged, this, &CVolumeViewer::onCollectionChanged);
    }
}

void CVolumeViewer::setSurface(const std::string &name)
{
    _surf_name = name;
    _surf = nullptr;
    _ptr = nullptr;
    onSurfaceChanged(name, _surf_col->surface(name));
}


void CVolumeViewer::invalidateVis()
{
    _slice_vis_valid = false;    
    for(auto &item : slice_vis_items) {
        fScene->removeItem(item);
        delete item;
    }
    slice_vis_items.resize(0);
}

void CVolumeViewer::invalidateIntersect(const std::string &name)
{
    if (!name.size() || name == _surf_name) {
        for(auto &pair : _intersect_items) {
            for(auto &item : pair.second) {
                fScene->removeItem(item);
                delete item;
            }
        }
        _intersect_items.clear();
    }
    else if (_intersect_items.count(name)) {
        for(auto &item : _intersect_items[name]) {
            fScene->removeItem(item);
            delete item;
        }
        _intersect_items.erase(name);
    }
}


void CVolumeViewer::onIntersectionChanged(std::string a, std::string b, Intersection *intersection)
{
    if (_ignore_intersect_change && intersection == _ignore_intersect_change)
        return;

    if (!_intersect_tgts.count(a) || !_intersect_tgts.count(b))
        return;

    //FIXME fix segmentation vs visible_segmentation naming and usage ..., think about dependency chain ..
    if (a == _surf_name || (_surf_name == "segmentation" && a == "visible_segmentation"))
        invalidateIntersect(b);
    else if (b == _surf_name || (_surf_name == "segmentation" && b == "visible_segmentation"))
        invalidateIntersect(a);
    
    renderIntersections();
}


std::set<std::string> CVolumeViewer::intersects()
{
    return _intersect_tgts;
}

void CVolumeViewer::setIntersects(const std::set<std::string> &set)
{
    _intersect_tgts = set;
    
    renderIntersections();
}

void CVolumeViewer::fitSurfaceInView()
{
    if (!_surf || !fGraphicsView) {
        return;
    }

    Rect3D bbox;
    bool haveBounds = false;

    if (auto* quadSurf = dynamic_cast<QuadSurface*>(_surf)) {
        bbox = quadSurf->bbox();
        haveBounds = true;
    } else if (auto* opChain = dynamic_cast<OpChain*>(_surf)) {
        QuadSurface* src = opChain->src();
        if (src) {
            bbox = src->bbox();
            haveBounds = true;
        }
    }

    if (!haveBounds) {
        // when we can't get bounds, just reset to a default view
        _scale = 1.0f;
        recalcScales();
        fGraphicsView->resetTransform();
        fGraphicsView->centerOn(0, 0);
        _lbl->setText(QString("%1x %2").arg(_scale).arg(_z_off));
        return;
    }

    // Calculate the actual dimensions of the bounding box
    float bboxWidth = bbox.high[0] - bbox.low[0];
    float bboxHeight = bbox.high[1] - bbox.low[1];

    if (bboxWidth <= 0 || bboxHeight <= 0) {
        return;
    }

    QSize viewportSize = fGraphicsView->viewport()->size();
    float viewportWidth = viewportSize.width();
    float viewportHeight = viewportSize.height();

    if (viewportWidth <= 0 || viewportHeight <= 0) {
        return;
    }

    // Calculate scale factor based on actual bbox dimensions
    float fit_factor = 0.8f;
    float required_scale_x = (viewportWidth * fit_factor) / bboxWidth;
    float required_scale_y = (viewportHeight * fit_factor) / bboxHeight;

    // Use the smaller scale to ensure the entire bbox fits
    float required_scale = std::min(required_scale_x, required_scale_y);

    _scale = required_scale;
    round_scale(_scale);
    recalcScales();

    fGraphicsView->resetTransform();
    fGraphicsView->centerOn(0, 0);

    _lbl->setText(QString("%1x %2").arg(_scale).arg(_z_off));
    curr_img_area = {0,0,0,0};
}


void CVolumeViewer::onSurfaceChanged(std::string name, Surface *surf)
{
    if (_surf_name == name) {
        _surf = surf;
        if (!_surf) {
            fScene->clear();
            // Clear all item collections when scene is cleared
            _intersect_items.clear();
            slice_vis_items.clear();
            _points_items.clear();
            _path_items.clear();
            _paths.clear();
            _cursor = nullptr;
            _center_marker = nullptr;
            fBaseImageItem = nullptr;
        }
        else {
            invalidateVis();
            if (name == "segmentation" && _resetViewOnSurfaceChange) {fitSurfaceInView();}
        }
    }

    //FIXME do not re-render surf if only segmentation changed?
    if (name == _surf_name) {
        curr_img_area = {0,0,0,0};
        renderVisible();
    }

    invalidateIntersect(name);
    renderIntersections();
}

QGraphicsItem *cursorItem(bool drawingMode = false, float brushSize = 3.0f, bool isSquare = false)
{
    if (drawingMode) {
        // Drawing mode cursor - shows brush shape and size
        QGraphicsItemGroup *group = new QGraphicsItemGroup();
        group->setZValue(10);
        
        QPen brushPen(QBrush(COLOR_CURSOR), 1.5);
        brushPen.setStyle(Qt::DashLine);
        
        // Draw brush shape
        if (isSquare) {
            float halfSize = brushSize / 2.0f;
            QGraphicsRectItem *rect = new QGraphicsRectItem(-halfSize, -halfSize, brushSize, brushSize);
            rect->setPen(brushPen);
            rect->setBrush(Qt::NoBrush);
            group->addToGroup(rect);
        } else {
            QGraphicsEllipseItem *circle = new QGraphicsEllipseItem(-brushSize/2, -brushSize/2, brushSize, brushSize);
            circle->setPen(brushPen);
            circle->setBrush(Qt::NoBrush);
            group->addToGroup(circle);
        }
        
        // Add small crosshair in center
        QPen centerPen(QBrush(COLOR_CURSOR), 1);
        QGraphicsLineItem *line = new QGraphicsLineItem(-2, 0, 2, 0);
        line->setPen(centerPen);
        group->addToGroup(line);
        line = new QGraphicsLineItem(0, -2, 0, 2);
        line->setPen(centerPen);
        group->addToGroup(line);
        
        return group;
    } else {
        // Regular cursor
        QPen pen(QBrush(COLOR_CURSOR), 2);
        QGraphicsLineItem *parent = new QGraphicsLineItem(-10, 0, -5, 0);
        parent->setZValue(10);
        parent->setPen(pen);
        QGraphicsLineItem *line = new QGraphicsLineItem(10, 0, 5, 0, parent);
        line->setPen(pen);
        line = new QGraphicsLineItem(0, -10, 0, -5, parent);
        line->setPen(pen);
        line = new QGraphicsLineItem(0, 10, 0, 5, parent);
        line->setPen(pen);
        
        return parent;
    }
}

QGraphicsItem *crossItem()
{
    QPen pen(QBrush(Qt::red), 1);
    QGraphicsLineItem *parent = new QGraphicsLineItem(-5, -5, 5, 5);
    parent->setZValue(10);
    parent->setPen(pen);
    QGraphicsLineItem *line = new QGraphicsLineItem(-5, 5, 5, -5, parent);
    line->setPen(pen);
    
    return parent;
}

//TODO make poi tracking optional and configurable
void CVolumeViewer::onPOIChanged(std::string name, POI *poi)
{    
    if (!poi || !_surf)
        return;
    
    if (name == "focus") {
        // Add safety check before dynamic_cast
        if (!_surf) {
            return;
        }
        
        PlaneSurface *plane = dynamic_cast<PlaneSurface*>(_surf);
        
        if (!plane)
            return;
        
        fGraphicsView->centerOn(0,0);
        
        if (poi->p == plane->origin())
            return;
        
        plane->setOrigin(poi->p);
        refreshPointPositions();
        
        _surf_col->setSurface(_surf_name, plane);
    }
    else if (name == "cursor") {
        // Add safety check before dynamic_cast
        if (!_surf) {
            return;
        }
        
        PlaneSurface *slice_plane = dynamic_cast<PlaneSurface*>(_surf);
        // QuadSurface *crop = dynamic_cast<QuadSurface*>(_surf_col->surface("visible_segmentation"));
        QuadSurface *crop = dynamic_cast<QuadSurface*>(_surf_col->surface("segmentation"));
        
        cv::Vec3f sp;
        float dist = -1;
        if (slice_plane) {            
            dist = slice_plane->pointDist(poi->p);
            sp = slice_plane->project(poi->p, 1.0, _scale);
        }
        else if (_surf_name == "segmentation" && crop)
        {
            SurfacePointer *ptr = crop->pointer();
            dist = crop->pointTo(ptr, poi->p, 2.0);
            sp = crop->loc(ptr)*_scale ;//+ cv::Vec3f(_vis_center[0],_vis_center[1],0);
        }
        
        if (!_cursor) {
            _cursor = cursorItem(_drawingModeActive, _brushSize, _brushIsSquare);
            fScene->addItem(_cursor);
        }
        
        if (dist != -1) {
            if (dist < 20.0/_scale) {
                _cursor->setPos(sp[0], sp[1]);
                _cursor->setOpacity(1.0-dist*_scale/20.0);
            }
            else
                _cursor->setOpacity(0.0);
        }
    }
}

cv::Mat CVolumeViewer::render_area(const cv::Rect &roi)
{
    cv::Mat_<cv::Vec3f> coords;
    cv::Mat_<uint8_t> img;

    // Check if we should use composite rendering
    if (_surf_name == "segmentation" && _composite_enabled && (_composite_layers_front > 0 || _composite_layers_behind > 0)) {
        // Composite rendering for segmentation view
        cv::Mat_<float> accumulator;
        int count = 0;
        
        // Alpha composition state for each pixel
        cv::Mat_<float> alpha_accumulator;
        cv::Mat_<float> value_accumulator;
        
        // Alpha composition parameters using the new settings
        const float alpha_min = _composite_alpha_min / 255.0f;
        const float alpha_max = _composite_alpha_max / 255.0f;
        const float alpha_opacity = _composite_material / 255.0f;
        const float alpha_cutoff = _composite_alpha_threshold / 10000.0f;
        
        // Determine the z range based on front and behind layers
        int z_start = _composite_reverse_direction ? -_composite_layers_behind : -_composite_layers_front;
        int z_end = _composite_reverse_direction ? _composite_layers_front : _composite_layers_behind;
        
        for (int z = z_start; z <= z_end; z++) {
            cv::Mat_<cv::Vec3f> slice_coords;
            cv::Mat_<uint8_t> slice_img;
            
            cv::Vec2f roi_c = {roi.x+roi.width/2, roi.y + roi.height/2};
            _ptr = _surf->pointer();
            cv::Vec3f diff = {roi_c[0],roi_c[1],0};
            _surf->move(_ptr, diff/_scale);
            _vis_center = roi_c;
            float z_step = z * _ds_scale;  // Scale the step to maintain consistent physical distance
            _surf->gen(&slice_coords, nullptr, roi.size(), _ptr, _scale, {-roi.width/2, -roi.height/2, _z_off + z_step});
            
            readInterpolated3D(slice_img, volume->zarrDataset(_ds_sd_idx), slice_coords*_ds_scale, cache);
            
            // Convert to float for accumulation
            cv::Mat_<float> slice_float;
            slice_img.convertTo(slice_float, CV_32F);
            
            if (_composite_method == "alpha") {
                // Alpha composition algorithm
                if (alpha_accumulator.empty()) {
                    alpha_accumulator = cv::Mat_<float>::zeros(slice_float.size());
                    value_accumulator = cv::Mat_<float>::zeros(slice_float.size());
                }
                
                // Process each pixel
                for (int y = 0; y < slice_float.rows; y++) {
                    for (int x = 0; x < slice_float.cols; x++) {
                        float pixel_value = slice_float(y, x);
                        
                        // Normalize pixel value
                        float normalized_value = (pixel_value / 255.0f - alpha_min) / (alpha_max - alpha_min);
                        normalized_value = std::max(0.0f, std::min(1.0f, normalized_value)); // Clamp to [0,1]
                        
                        // Skip empty areas (speed through)
                        if (normalized_value == 0.0f) {
                            continue;
                        }
                        
                        float current_alpha = alpha_accumulator(y, x);
                        
                        // Check alpha cutoff for early termination
                        if (current_alpha >= alpha_cutoff) {
                            continue;
                        }
                        
                        // Calculate weight
                        float weight = (1.0f - current_alpha) * std::min(normalized_value * alpha_opacity, 1.0f);
                        
                        // Accumulate
                        value_accumulator(y, x) += weight * normalized_value;
                        alpha_accumulator(y, x) += weight;
                    }
                }
            } else {
                // Original composite methods
                if (accumulator.empty()) {
                    accumulator = slice_float;
                    if (_composite_method == "min") {
                        accumulator.setTo(255.0); // Initialize to max value for min operation
                        accumulator = cv::min(accumulator, slice_float);
                    }
                } else {
                    if (_composite_method == "max") {
                        accumulator = cv::max(accumulator, slice_float);
                    } else if (_composite_method == "mean") {
                        accumulator += slice_float;
                        count++;
                    } else if (_composite_method == "min") {
                        accumulator = cv::min(accumulator, slice_float);
                    }
                }
            }
        }
        
        // Finalize alpha composition result
        if (_composite_method == "alpha") {
            accumulator = cv::Mat_<float>::zeros(value_accumulator.size());
            for (int y = 0; y < value_accumulator.rows; y++) {
                for (int x = 0; x < value_accumulator.cols; x++) {
                    float final_value = value_accumulator(y, x) * 255.0f;
                    accumulator(y, x) = std::max(0.0f, std::min(255.0f, final_value)); // Clamp to [0,255]
                }
            }
        }
        
        // Convert back to uint8
        if (_composite_method == "mean" && count > 0) {
            accumulator /= count;
        }
        accumulator.convertTo(img, CV_8U);
        
        return img;
    }
    else {
        // Standard single-slice rendering
        //PlaneSurface use absolute positioning to simplify intersection logic
        if (dynamic_cast<PlaneSurface*>(_surf)) {
            _surf->gen(&coords, nullptr, roi.size(), nullptr, _scale, {roi.x, roi.y, _z_off});
        }
        else {
            cv::Vec2f roi_c = {roi.x+roi.width/2, roi.y + roi.height/2};

            _ptr = _surf->pointer();
            cv::Vec3f diff = {roi_c[0],roi_c[1],0};
            _surf->move(_ptr, diff/_scale);
            _vis_center = roi_c;
            _surf->gen(&coords, nullptr, roi.size(), _ptr, _scale, {-roi.width/2, -roi.height/2, _z_off});
        }

        readInterpolated3D(img, volume->zarrDataset(_ds_sd_idx), coords*_ds_scale, cache);
        return img;
    }
}

class LifeTime
{
public:
    LifeTime(std::string msg)
    {
        std::cout << msg << std::flush;
        start = std::chrono::high_resolution_clock::now();
    }
    ~LifeTime()
    {
        auto end = std::chrono::high_resolution_clock::now();
        std::cout << " took " << std::chrono::duration<double>(end-start).count() << " s" << std::endl;
    }
private:
    std::chrono::time_point<std::chrono::high_resolution_clock> start;
};

void CVolumeViewer::renderVisible(bool force)
{
    if (!volume || !volume->zarrDataset() || !_surf)
        return;
    
    QRectF bbox = fGraphicsView->mapToScene(fGraphicsView->viewport()->geometry()).boundingRect();
    
    if (!force && QRectF(curr_img_area).contains(bbox))
        return;
    
    renderPaths();
    
    curr_img_area = {bbox.left()-128,bbox.top()-128, bbox.width()+256, bbox.height()+256};
    
    cv::Mat img = render_area({curr_img_area.x(), curr_img_area.y(), curr_img_area.width(), curr_img_area.height()});
    
    QImage qimg = Mat2QImage(img);
    
    QPixmap pixmap = QPixmap::fromImage(qimg, fSkipImageFormatConv ? Qt::NoFormatConversion : Qt::AutoColor);
 
    // Add the QPixmap to the scene as a QGraphicsPixmapItem
    if (!fBaseImageItem)
        fBaseImageItem = fScene->addPixmap(pixmap);
    else
        fBaseImageItem->setPixmap(pixmap);
    
    if (!_center_marker) {
        _center_marker = fScene->addEllipse({-10,-10,20,20}, QPen(COLOR_FOCUS, 3, Qt::DashDotLine, Qt::RoundCap, Qt::RoundJoin));
        _center_marker->setZValue(11);
    }

    _center_marker->setParentItem(fBaseImageItem);
    
    fBaseImageItem->setOffset(curr_img_area.topLeft());
}

struct vec3f_hash {
    size_t operator()(cv::Vec3f p) const
    {
        size_t hash1 = std::hash<float>{}(p[0]);
        size_t hash2 = std::hash<float>{}(p[1]);
        size_t hash3 = std::hash<float>{}(p[2]);
        
        //magic numbers from boost. should be good enough
        size_t hash = hash1  ^ (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2));
        return hash  ^ (hash3 + 0x9e3779b9 + (hash << 6) + (hash >> 2));
    }
};

void CVolumeViewer::renderIntersections()
{
    if (!volume || !volume->zarrDataset() || !_surf)
        return;
    
    std::vector<std::string> remove;
    for (auto &pair : _intersect_items)
        if (!_intersect_tgts.count(pair.first)) {
            for(auto &item : pair.second) {
                fScene->removeItem(item);
                delete item;
            }
            remove.push_back(pair.first);
        }
    for(auto key : remove)
        _intersect_items.erase(key);

    PlaneSurface *plane = dynamic_cast<PlaneSurface*>(_surf);
    
    if (_z_off)
        return;
    
    if (plane) {
        cv::Rect plane_roi = {curr_img_area.x()/_scale, curr_img_area.y()/_scale, curr_img_area.width()/_scale, curr_img_area.height()/_scale};

        cv::Vec3f corner = plane->coord(nullptr, {plane_roi.x, plane_roi.y, 0.0});
        Rect3D view_bbox = {corner, corner};
        view_bbox = expand_rect(view_bbox, plane->coord(nullptr, {plane_roi.br().x, plane_roi.y, 0}));
        view_bbox = expand_rect(view_bbox, plane->coord(nullptr, {plane_roi.x, plane_roi.br().y, 0}));
        view_bbox = expand_rect(view_bbox, plane->coord(nullptr, {plane_roi.br().x, plane_roi.br().y, 0}));

        std::vector<std::string> intersect_cands;
        std::vector<std::string> intersect_tgts_v;

        for (auto key : _intersect_tgts)
            intersect_tgts_v.push_back(key);

#pragma omp parallel for
        for(int n=0;n<intersect_tgts_v.size();n++) {
            std::string key = intersect_tgts_v[n];
            bool haskey;
#pragma omp critical
            haskey = _intersect_items.count(key);
            if (!haskey && dynamic_cast<QuadSurface*>(_surf_col->surface(key))) {
                QuadSurface *segmentation = dynamic_cast<QuadSurface*>(_surf_col->surface(key));

                if (intersect(view_bbox, segmentation->bbox()))
#pragma omp critical
                    intersect_cands.push_back(key);
                else
#pragma omp critical
                    _intersect_items[key] = {};
            }
        }

        std::vector<std::vector<std::vector<cv::Vec3f>>> intersections(intersect_cands.size());

#pragma omp parallel for
        for(int n=0;n<intersect_cands.size();n++) {
            std::string key = intersect_cands[n];
            QuadSurface *segmentation = dynamic_cast<QuadSurface*>(_surf_col->surface(key));

            std::vector<std::vector<cv::Vec2f>> xy_seg_;
            if (key == "segmentation") {
                find_intersect_segments(intersections[n], xy_seg_, segmentation->rawPoints(), plane, plane_roi, 4/_scale, 1000);
            }
            else
                find_intersect_segments(intersections[n], xy_seg_, segmentation->rawPoints(), plane, plane_roi, 4/_scale);

        }

        std::hash<std::string> str_hasher;

        for(int n=0;n<intersect_cands.size();n++) {
            std::string key = intersect_cands[n];

            if (!intersections.size()) {
                _intersect_items[key] = {};
                continue;
            }

            size_t seed = str_hasher(key);
            srand(seed);

            int prim = rand() % 3;
            cv::Vec3i cvcol = {100 + rand() % 255, 100 + rand() % 255, 100 + rand() % 255};
            cvcol[prim] = 200 + rand() % 55;

            QColor col(cvcol[0],cvcol[1],cvcol[2]);
            float width = 2;
            int z_value = 5;

            if (key == "segmentation") {
                col =
                    (_surf_name == "seg yz"   ? COLOR_SEG_YZ
                     : _surf_name == "seg xz" ? COLOR_SEG_XZ
                                              : COLOR_SEG_XY);
                width = 3;
                z_value = 20;
            }


            QuadSurface *segmentation = dynamic_cast<QuadSurface*>(_surf_col->surface(intersect_cands[n]));
            std::vector<QGraphicsItem*> items;

            int len = 0;
            for (auto seg : intersections[n]) {
                QPainterPath path;

                bool first = true;
                cv::Vec3f last = {-1,-1,-1};
                for (auto wp : seg)
                {
                    len++;
                    cv::Vec3f p = plane->project(wp, 1.0, _scale);

                    if (last[0] != -1 && cv::norm(p-last) >= 8) {
                        auto item = fGraphicsView->scene()->addPath(path, QPen(col, width));
                        item->setZValue(z_value);
                        items.push_back(item);
                        first = true;
                    }
                    last = p;

                    if (first)
                        path.moveTo(p[0],p[1]);
                    else
                        path.lineTo(p[0],p[1]);
                    first = false;
                }
                auto item = fGraphicsView->scene()->addPath(path, QPen(col, width));
                item->setZValue(z_value);
                items.push_back(item);
            }
            _intersect_items[key] = items;
            _ignore_intersect_change = new Intersection({intersections[n]});
            _surf_col->setIntersection(_surf_name, key, _ignore_intersect_change);
            _ignore_intersect_change = nullptr;
        }
    }
    else if (_surf_name == "segmentation" /*&& dynamic_cast<QuadSurface*>(_surf_col->surface("visible_segmentation"))*/) {
        // QuadSurface *crop = dynamic_cast<QuadSurface*>(_surf_col->surface("visible_segmentation"));

        //TODO make configurable, for now just show everything!
        std::vector<std::pair<std::string,std::string>> intersects = _surf_col->intersections("segmentation");
        for(auto pair : intersects) {
            std::string key = pair.first;
            if (key == "segmentation")
                key = pair.second;
            
            if (_intersect_items.count(key) || !_intersect_tgts.count(key))
                continue;
            
            std::unordered_map<cv::Vec3f,cv::Vec3f,vec3f_hash> location_cache;
            std::vector<cv::Vec3f> src_locations;

            for (auto seg : _surf_col->intersection(pair.first, pair.second)->lines)
                for (auto wp : seg)
                    src_locations.push_back(wp);
            
#pragma omp parallel
            {
                // SurfacePointer *ptr = crop->pointer();
                SurfacePointer *ptr = _surf->pointer();
#pragma omp for
                for (auto wp : src_locations) {
                    // float res = crop->pointTo(ptr, wp, 2.0, 100);
                    // cv::Vec3f p = crop->loc(ptr)*_ds_scale + cv::Vec3f(_vis_center[0],_vis_center[1],0);
                    float res = _surf->pointTo(ptr, wp, 2.0, 100);
                    cv::Vec3f p = _surf->loc(ptr)*_scale ;//+ cv::Vec3f(_vis_center[0],_vis_center[1],0);
                    //FIXME still happening?
                    if (res >= 2.0)
                        p = {-1,-1,-1};
                        // std::cout << "WARNING pointTo() high residual in renderIntersections()" << std::endl;
#pragma omp critical
                    location_cache[wp] = p;
                }
            }
            
            std::vector<QGraphicsItem*> items;
            for (auto seg : _surf_col->intersection(pair.first, pair.second)->lines) {
                QPainterPath path;
                
                bool first = true;
                cv::Vec3f last = {-1,-1,-1};
                for (auto wp : seg)
                {
                    cv::Vec3f p = location_cache[wp];
                    
                    if (p[0] == -1)
                        continue;

                    if (last[0] != -1 && cv::norm(p-last) >= 8) {
                        auto item = fGraphicsView->scene()->addPath(path, QPen(key == "seg yz" ? COLOR_SEG_YZ: COLOR_SEG_XZ, 2));
                        item->setZValue(5);
                        items.push_back(item);
                        first = true;
                    }
                    last = p;

                    if (first)
                        path.moveTo(p[0],p[1]);
                    else
                        path.lineTo(p[0],p[1]);
                    first = false;
                }
                auto item = fGraphicsView->scene()->addPath(path, QPen(key == "seg yz" ? COLOR_SEG_YZ: COLOR_SEG_XZ, 2));
                item->setZValue(5);
                items.push_back(item);
            }
            _intersect_items[key] = items;
        }
    }
}


void CVolumeViewer::onPanRelease(Qt::MouseButton buttons, Qt::KeyboardModifiers modifiers)
{
    renderVisible();

    // Defer intersection rendering
    if (dynamic_cast<PlaneSurface*>(_surf)) {
        _deferredRenderIntersections = true;
        _deferredUpdateTimer->stop();
        _deferredUpdateTimer->start();
    }
}

void CVolumeViewer::onPanStart(Qt::MouseButton buttons, Qt::KeyboardModifiers modifiers)
{
    renderVisible();
    
    // Hide intersections immediately for smooth panning
    for(auto &col : _intersect_items)
        for(auto &item : col.second)
            item->setVisible(false);

    // Defer the expensive invalidation
    if (dynamic_cast<PlaneSurface*>(_surf)) {
        _deferredInvalidateIntersect = true;
        _deferredUpdateTimer->stop();
        _deferredUpdateTimer->start();
    }
}

void CVolumeViewer::onScrolled()
{
    // if (!dynamic_cast<OpChain*>(_surf) && !dynamic_cast<OpChain*>(_surf)->slow() && _min_scale == 1.0)
        // renderVisible();
    // if ((!dynamic_cast<OpChain*>(_surf) || !dynamic_cast<OpChain*>(_surf)->slow()) && _min_scale < 1.0)
        // renderVisible();
}

void CVolumeViewer::renderPaths()
{
    // Clear existing path items
    for(auto &item : _path_items) {
        if (item && item->scene() == fScene) {
            fScene->removeItem(item);
        }
        delete item;
    }
    _path_items.clear();
    
    if (!_surf) {
        return;
    }
    
    // Separate paths by type for proper rendering order
    QList<PathData> drawPaths;
    QList<PathData> eraserPaths;
    
    for (const auto& path : _paths) {
        if (path.isEraser) {
            eraserPaths.append(path);
        } else {
            drawPaths.append(path);
        }
    }
    
    // First render regular drawing paths
    for (const auto& path : drawPaths) {
        if (path.points.size() < 2) {
            continue;
        }
        
        QPainterPath painterPath;
        bool firstPoint = true;
        
        PlaneSurface *plane = dynamic_cast<PlaneSurface*>(_surf);
        QuadSurface *quad = dynamic_cast<QuadSurface*>(_surf);
        
        for (const auto& wp : path.points) {
            cv::Vec3f p;
            
            if (plane) {
                if (plane->pointDist(wp) >= 4.0)
                    continue;
                p = plane->project(wp, 1.0, _scale);
            }
            else if (quad) {
                SurfacePointer *ptr = quad->pointer();
                float res = _surf->pointTo(ptr, wp, 4.0, 100);
                p = _surf->loc(ptr)*_scale;
                if (res >= 4.0)
                    continue;
            }
            else
                continue;
            
            if (firstPoint) {
                painterPath.moveTo(p[0], p[1]);
                firstPoint = false;
            } else {
                painterPath.lineTo(p[0], p[1]);
            }
        }
        
        // Create the path item with the specified color and properties
        QColor color = path.color;
        if (path.opacity < 1.0f) {
            color.setAlphaF(path.opacity);
        }
        
        QPen pen(color, path.lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        
        // Apply different brush shapes
        if (path.brushShape == PathData::BrushShape::SQUARE) {
            pen.setCapStyle(Qt::SquareCap);
            pen.setJoinStyle(Qt::MiterJoin);
        }
        
        auto item = fScene->addPath(painterPath, pen);
        item->setZValue(25); // Higher than intersections but lower than points
        _path_items.push_back(item);
    }
    
    // Then render eraser paths with a distinctive style
    // In the actual mask generation, these will subtract from the drawn areas
    for (const auto& path : eraserPaths) {
        if (path.points.size() < 2) {
            continue;
        }
        
        QPainterPath painterPath;
        bool firstPoint = true;
        
        PlaneSurface *plane = dynamic_cast<PlaneSurface*>(_surf);
        QuadSurface *quad = dynamic_cast<QuadSurface*>(_surf);
        
        for (const auto& wp : path.points) {
            cv::Vec3f p;
            
            if (plane) {
                if (plane->pointDist(wp) >= 4.0)
                    continue;
                p = plane->project(wp, 1.0, _scale);
            }
            else if (quad) {
                SurfacePointer *ptr = quad->pointer();
                float res = _surf->pointTo(ptr, wp, 4.0, 100);
                p = _surf->loc(ptr)*_scale;
                if (res >= 4.0)
                    continue;
            }
            else
                continue;
            
            if (firstPoint) {
                painterPath.moveTo(p[0], p[1]);
                firstPoint = false;
            } else {
                painterPath.lineTo(p[0], p[1]);
            }
        }
        
        // Render eraser paths with a distinctive appearance
        // Using a dashed pattern to indicate eraser mode
        QPen pen(Qt::red, path.lineWidth, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
        pen.setDashPattern(QVector<qreal>() << 4 << 4);
        
        if (path.opacity < 1.0f) {
            QColor eraserColor = pen.color();
            eraserColor.setAlphaF(path.opacity);
            pen.setColor(eraserColor);
        }
        
        auto item = fScene->addPath(painterPath, pen);
        item->setZValue(26); // Slightly higher than regular paths
        _path_items.push_back(item);
    }
}

void CVolumeViewer::renderOrUpdatePoint(const ColPoint& point)
{
    if (!_surf) return;

    QPointF scene_pos = volumeToScene(point.p);
    float radius = 5.0f; // pixels
    
    const auto& collections = _point_collection->getAllCollections();
    auto col_it = collections.find(point.collectionId);
    cv::Vec3f cv_color = (col_it != collections.end()) ? col_it->second.color : cv::Vec3f(1,0,0);
    QColor color(cv_color[0] * 255, cv_color[1] * 255, cv_color[2] * 255, 255);

    QColor border_color(255, 255, 255, 200);
    float border_width = 1.5f;

    if (point.id == _highlighted_point_id) {
        radius = 7.0f;
        border_color = Qt::yellow;
        border_width = 2.5f;
    }
 
    if (point.id == _selected_point_id) {
        border_color = QColor(255, 0, 255, 255); // Bright magenta for selection
        border_width = 2.5f;
        radius = 7.0f;
    }

    PointGraphics pg;
    bool exists = _points_items.count(point.id);
    if (exists) {
        pg = _points_items[point.id];
    }

    // Update circle
    if (exists) {
        pg.circle->setRect(scene_pos.x() - radius, scene_pos.y() - radius, radius * 2, radius * 2);
        pg.circle->setPen(QPen(border_color, border_width));
        pg.circle->setBrush(QBrush(color));
    } else {
        pg.circle = fScene->addEllipse(
            scene_pos.x() - radius, scene_pos.y() - radius, radius * 2, radius * 2,
            QPen(border_color, border_width), QBrush(color)
        );
        pg.circle->setZValue(10);
    }

    // Update or create text
    bool has_winding = !std::isnan(point.winding_annotation);
    if (exists) {
        pg.text->setPos(scene_pos.x() + radius, scene_pos.y() - radius);
        pg.text->setVisible(has_winding);
    } else {
        pg.text = new COutlinedTextItem();
        fScene->addItem(pg.text);
        pg.text->setZValue(11); // Above points
        pg.text->setDefaultTextColor(Qt::white);
        pg.text->setPos(scene_pos.x() + radius, scene_pos.y() - radius);
        pg.text->setVisible(has_winding);
    }
    
    if (has_winding) {
        bool absolute = col_it != collections.end() ? col_it->second.metadata.absolute_winding_number : false;
        
        // Adaptive decimal formatting
        QString num_text = QString::number(point.winding_annotation, 'g');

        if (!absolute) {
            if (point.winding_annotation >= 0) {
                num_text.prepend("+");
            }
        }
        
        pg.text->setPlainText(num_text);

        // Fixed positioning
        pg.text->setPos(scene_pos.x() + radius, scene_pos.y() - radius);
    }

    if (!exists) {
        _points_items[point.id] = pg;
    }
}

void CVolumeViewer::onPathsChanged(const QList<PathData>& paths)
{
    _paths = paths;
    renderPaths();
}

void CVolumeViewer::onMousePress(QPointF scene_loc, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
    if (!_point_collection || !_surf) return;

    if (button == Qt::LeftButton) {
        if (_highlighted_point_id != 0) {
           if (_selected_point_id != _highlighted_point_id) {
               uint64_t old_selected_id = _selected_point_id;
               _selected_point_id = _highlighted_point_id;
               emit pointSelected(_selected_point_id);

               if (auto old_point = _point_collection->getPoint(old_selected_id)) {
                   renderOrUpdatePoint(*old_point);
               }
               if (auto new_point = _point_collection->getPoint(_selected_point_id)) {
                   renderOrUpdatePoint(*new_point);
               }
           }
            _dragged_point_id = _highlighted_point_id;
            // Do not return, allow forwarding for other widgets
        }
    } else if (button == Qt::RightButton) {
        if (_highlighted_point_id != 0) {
            _point_collection->removePoint(_highlighted_point_id);
        }
    }

    // Forward for drawing widgets
    cv::Vec3f p, n;
    if (scene2vol(p, n, _surf, _surf_name, _surf_col, scene_loc, _vis_center, _scale)) {
        sendMousePressVolume(p, n, button, modifiers);
    }
}

void CVolumeViewer::onMouseMove(QPointF scene_loc, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers)
{
    onCursorMove(scene_loc); // Keep highlighting up to date

    if ((buttons & Qt::LeftButton) && _dragged_point_id != 0) {
        cv::Vec3f p, n;
        if (scene2vol(p, n, _surf, _surf_name, _surf_col, scene_loc, _vis_center, _scale)) {
            if (auto point_opt = _point_collection->getPoint(_dragged_point_id)) {
                ColPoint updated_point = *point_opt;
                updated_point.p = p;
                _point_collection->updatePoint(updated_point);
            }
        }
    } else {
        if (!_surf) {
            return;
        }
        
        cv::Vec3f p, n;
        if (!scene2vol(p, n, _surf, _surf_name, _surf_col, scene_loc, _vis_center, _scale))
            return;
        
        emit sendMouseMoveVolume(p, buttons, modifiers);
    }
}

void CVolumeViewer::onMouseRelease(QPointF scene_loc, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
    if (button == Qt::LeftButton && _dragged_point_id != 0) {
        _dragged_point_id = 0;
        // Re-run highlight logic
        onCursorMove(scene_loc);
    }

    // Forward for drawing widgets
    cv::Vec3f p, n;
    if (scene2vol(p, n, _surf, _surf_name, _surf_col, scene_loc, _vis_center, _scale)) {
        if (dynamic_cast<PlaneSurface*>(_surf))
            emit sendMouseReleaseVolume(p, button, modifiers);
        else if (_surf_name == "segmentation")
            emit sendMouseReleaseVolume(p, button, modifiers);
        else
            std::cout << "FIXME: onMouseRelease()" << std::endl;
    }
}

void CVolumeViewer::setCompositeEnabled(bool enabled)
{
    if (_composite_enabled != enabled) {
        _composite_enabled = enabled;
        renderVisible(true);
        
        // Update status label
        QString status = QString("%1x %2").arg(_scale).arg(_z_off);
        if (_composite_enabled) {
            QString method = QString::fromStdString(_composite_method);
            method[0] = method[0].toUpper();
            status += QString(" | Composite: %1(%2)").arg(method).arg(_composite_layers);
        }
        _lbl->setText(status);
    }
}

void CVolumeViewer::setCompositeLayers(int layers)
{
    if (layers >= 1 && layers <= 21 && layers != _composite_layers) {
        _composite_layers = layers;
        if (_composite_enabled) {
            renderVisible(true);
            
            // Update status label
            QString status = QString("%1x %2").arg(_scale).arg(_z_off);
            QString method = QString::fromStdString(_composite_method);
            method[0] = method[0].toUpper();
            status += QString(" | Composite: %1(%2)").arg(method).arg(_composite_layers);
            _lbl->setText(status);
        }
    }
}

void CVolumeViewer::setCompositeLayersInFront(int layers)
{
    if (layers >= 0 && layers <= 21 && layers != _composite_layers_front) {
        _composite_layers_front = layers;
        if (_composite_enabled) {
            renderVisible(true);
        }
    }
}

void CVolumeViewer::setCompositeLayersBehind(int layers)
{
    if (layers >= 0 && layers <= 21 && layers != _composite_layers_behind) {
        _composite_layers_behind = layers;
        if (_composite_enabled) {
            renderVisible(true);
        }
    }
}

void CVolumeViewer::setCompositeAlphaMin(int value)
{
    if (value >= 0 && value <= 255 && value != _composite_alpha_min) {
        _composite_alpha_min = value;
        if (_composite_enabled && _composite_method == "alpha") {
            renderVisible(true);
        }
    }
}

void CVolumeViewer::setCompositeAlphaMax(int value)
{
    if (value >= 0 && value <= 255 && value != _composite_alpha_max) {
        _composite_alpha_max = value;
        if (_composite_enabled && _composite_method == "alpha") {
            renderVisible(true);
        }
    }
}

void CVolumeViewer::setCompositeAlphaThreshold(int value)
{
    if (value >= 0 && value <= 10000 && value != _composite_alpha_threshold) {
        _composite_alpha_threshold = value;
        if (_composite_enabled && _composite_method == "alpha") {
            renderVisible(true);
        }
    }
}

void CVolumeViewer::setCompositeMaterial(int value)
{
    if (value >= 0 && value <= 255 && value != _composite_material) {
        _composite_material = value;
        if (_composite_enabled && _composite_method == "alpha") {
            renderVisible(true);
        }
    }
}

void CVolumeViewer::setCompositeReverseDirection(bool reverse)
{
    if (reverse != _composite_reverse_direction) {
        _composite_reverse_direction = reverse;
        if (_composite_enabled) {
            renderVisible(true);
        }
    }
}

void CVolumeViewer::setCompositeMethod(const std::string& method)
{
    if (method != _composite_method && (method == "max" || method == "mean" || method == "min" || method == "alpha")) {
        _composite_method = method;
        if (_composite_enabled) {
            renderVisible(true);
            
            // Update status label
            QString status = QString("%1x %2").arg(_scale).arg(_z_off);
            QString methodDisplay = QString::fromStdString(_composite_method);
            methodDisplay[0] = methodDisplay[0].toUpper();
            status += QString(" | Composite: %1(%2)").arg(methodDisplay).arg(_composite_layers);
            _lbl->setText(status);
        }
    }
}

void CVolumeViewer::onVolumeClosing()
{
    // Only clear segmentation-related surfaces, not persistent plane surfaces
    if (_surf_name == "segmentation") {
        onSurfaceChanged(_surf_name, nullptr);
    }
    // For plane surfaces (xy plane, xz plane, yz plane), just clear the scene
    // but keep the surface reference so it can render with the new volume
    else if (_surf_name == "xy plane" || _surf_name == "xz plane" || _surf_name == "yz plane") {
        if (fScene) {
            fScene->clear();
        }
        // Clear all item collections
        _intersect_items.clear();
        slice_vis_items.clear();
        _points_items.clear();
        _path_items.clear();
        _paths.clear();
        _cursor = nullptr;
        _center_marker = nullptr;
        fBaseImageItem = nullptr;
        // Note: We don't set _surf = nullptr here, so the surface remains available
    }
    else {
        // For other surface types (seg xz, seg yz), clear them
        onSurfaceChanged(_surf_name, nullptr);
    }
}

void CVolumeViewer::onDrawingModeActive(bool active, float brushSize, bool isSquare)
{
    _drawingModeActive = active;
    _brushSize = brushSize;
    _brushIsSquare = isSquare;
    
    // Update the cursor to reflect the drawing mode state
    if (_cursor) {
        fScene->removeItem(_cursor);
        delete _cursor;
        _cursor = nullptr;
    }
    
    // Force cursor update
    POI *cursor = _surf_col->poi("cursor");
    if (cursor) {
        onPOIChanged("cursor", cursor);
    }
}

void CVolumeViewer::refreshPointPositions()
{
    if (!_point_collection) {
        return;
    }

    for (const auto& col_pair : _point_collection->getAllCollections()) {
        for (const auto& point_pair : col_pair.second.points) {
            if (_points_items.count(point_pair.first)) {
                renderOrUpdatePoint(point_pair.second);
            }
        }
    }
}
void CVolumeViewer::onPointAdded(const ColPoint& point)
{
    renderOrUpdatePoint(point);
}

void CVolumeViewer::onPointChanged(const ColPoint& point)
{
    renderOrUpdatePoint(point);
}

void CVolumeViewer::onPointRemoved(uint64_t pointId)
{
    if (_points_items.count(pointId)) {
        auto& pg = _points_items[pointId];
        fScene->removeItem(pg.circle);
        fScene->removeItem(pg.text);
        delete pg.circle;
        delete pg.text;
        _points_items.erase(pointId);
    }
}

void CVolumeViewer::onCollectionSelected(uint64_t collectionId)
{
    _selected_collection_id = collectionId;
}

void CVolumeViewer::onCollectionChanged(uint64_t collectionId)
{
    if (!_point_collection) {
        return;
    }

    const auto& collections = _point_collection->getAllCollections();
    auto it = collections.find(collectionId);
    if (it != collections.end()) {
        const auto& collection = it->second;
        for (const auto& point_pair : collection.points) {
            renderOrUpdatePoint(point_pair.second);
        }
    }
}

void CVolumeViewer::onKeyRelease(int key, Qt::KeyboardModifiers modifiers)
{
    if (key == Qt::Key_Shift) {
        _new_shift_group_required = true;
    }
}

void CVolumeViewer::onPointSelected(uint64_t pointId)
{
    if (_selected_point_id == pointId) {
        return;
    }

    uint64_t old_selected_id = _selected_point_id;
    _selected_point_id = pointId;

    if (auto old_point = _point_collection->getPoint(old_selected_id)) {
        renderOrUpdatePoint(*old_point);
    }
    if (auto new_point = _point_collection->getPoint(_selected_point_id)) {
        renderOrUpdatePoint(*new_point);
    }
}

void CVolumeViewer::setResetViewOnSurfaceChange(bool reset)
{
    _resetViewOnSurfaceChange = reset;
}

void CVolumeViewer::performDeferredUpdates()
{
    if (_deferredInvalidateVis) {
        invalidateVis();
        _deferredInvalidateVis = false;
    }

    if (_deferredInvalidateIntersect) {
        invalidateIntersect();
        _deferredInvalidateIntersect = false;
    }

    if (_deferredRenderIntersections) {
        renderIntersections();
        _deferredRenderIntersections = false;
    }

    // Force a final high-quality render
    renderVisible(true);
}