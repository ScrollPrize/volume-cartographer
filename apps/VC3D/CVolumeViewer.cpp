// CVolumeViewer.cpp
// Chao Du 2015 April
#include "CVolumeViewer.hpp"
#include "UDataManipulateUtils.hpp"
#include "HBase.hpp"

#include <QGraphicsView>
#include <QGraphicsScene>

#include "CVolumeViewerView.hpp"
#include "CSurfaceCollection.hpp"

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
#define ZOOM_FACTOR 1.15 // Changed from 2.0 (which was too aggressive)

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

    // Create graphics scene
    fScene = new QGraphicsScene({-2500,-2500,5000,5000}, this);

    // Set the scene
    fGraphicsView->setScene(fScene);

    QSettings settings("VC.ini", QSettings::IniFormat);
    // fCenterOnZoomEnabled = settings.value("viewer/center_on_zoom", false).toInt() != 0;
    // fScrollSpeed = settings.value("viewer/scroll_speed", false).toInt();
    fSkipImageFormatConv = settings.value("perf/chkSkipImageFormatConvExp", false).toBool();

    QVBoxLayout* aWidgetLayout = new QVBoxLayout;
    aWidgetLayout->addWidget(fGraphicsView);

    setLayout(aWidgetLayout);


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
    if (abs(scale-round(log2(scale))) < 0.02)
        scale = pow(2,round(log2(scale)));
}

//get center of current visible area in scene coordinates
QPointF visible_center(QGraphicsView *view)
{
    QRectF bbox = view->mapToScene(view->viewport()->geometry()).boundingRect();
    return bbox.topLeft() + QPointF(bbox.width(),bbox.height())*0.5;
}


void scene2vol(cv::Vec3f &p, cv::Vec3f &n, Surface *_surf, const std::string &_surf_name, CSurfaceCollection *_surf_col, const QPointF &scene_loc, const cv::Vec2f &_vis_center, float _ds_scale)
{
    // Safety check for null surface
    if (!_surf) {
        p = cv::Vec3f(0, 0, 0);
        n = cv::Vec3f(0, 0, 1);
        return;
    }
    
    //for PlaneSurface we work with absolute coordinates only
    // if (dynamic_cast<PlaneSurface*>(_surf)) {
        cv::Vec3f surf_loc = {scene_loc.x()/_ds_scale, scene_loc.y()/_ds_scale,0};
        
        SurfacePointer *ptr = _surf->pointer();
        
        n = _surf->normal(ptr, surf_loc);
        p = _surf->coord(ptr, surf_loc);
//     }
//     //FIXME quite some assumptions ...
//     else if (_surf_name == "segmentation") {
//         // assert(_ptr);
//         assert(dynamic_cast<OpChain*>(_surf));
//         
//         QuadSurface* crop = dynamic_cast<QuadSurface*>(_surf_col->surface("visible_segmentation")); 
//         
//         cv::Vec3f delta = {(scene_loc.x()-_vis_center[0])/_ds_scale, (scene_loc.y()-_vis_center[1])/_ds_scale,0};
//         
//         //NOTE crop center and original scene _ptr are off by < 0.5 voxels?
//         SurfacePointer *ptr = crop->pointer();
//         n = crop->normal(ptr, delta);
//         p = crop->coord(ptr, delta);
//     }
}

void CVolumeViewer::onCursorMove(QPointF scene_loc)
{
    if (!_surf || !_surf_col)
        return;

    POI *cursor = _surf_col->poi("cursor");
    if (!cursor)
        cursor = new POI;
    
    cv::Vec3f p, n;
    scene2vol(p, n, _surf, _surf_name, _surf_col, scene_loc, _vis_center, _scale);
    cursor->p = p;
    
    _surf_col->setPOI("cursor", cursor);
}

void CVolumeViewer::recalcScales()
{
    // if (dynamic_cast<PlaneSurface*>(_surf))
        _min_scale = pow(2.0,1.-volume->numScales());
    // else
        // _min_scale = std::max(pow(2.0,1.-volume->numScales()), 0.5);
    
    if (_scale >= _max_scale) {
        _ds_scale = _max_scale;
        _ds_sd_idx = -log2(_ds_scale);
    }
    else if (_scale < _min_scale) {
        _ds_scale = _min_scale;
        _ds_sd_idx = -log2(_ds_scale);
    }
    else {
        _ds_sd_idx = -log2(_scale);
        _ds_scale = pow(2,-_ds_sd_idx);
    }
}

void CVolumeViewer::onZoom(int steps, QPointF scene_loc, Qt::KeyboardModifiers modifiers)
{
    invalidateVis();
    invalidateIntersect();
    
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
        
        _scale *= zoom;
        round_scale(_scale);
        
        recalcScales();
        
        curr_img_area = {0,0,0,0};
        QPointF center = visible_center(fGraphicsView) * zoom;
        
        //FIXME get correct size for slice!
        int max_size = 100000 ;//std::max(volume->sliceWidth(), std::max(volume->numSlices(), volume->sliceHeight()))*_ds_scale + 512;
        fGraphicsView->setSceneRect(-max_size/2,-max_size/2,max_size,max_size);
        
        fGraphicsView->centerOn(center);
        renderVisible();
    }

    _lbl->setText(QString("%1x %2").arg(_scale).arg(_z_off));
    
    renderIntersections();
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
}

void CVolumeViewer::onVolumeClicked(QPointF scene_loc, Qt::MouseButton buttons, Qt::KeyboardModifiers modifiers)
{
    if (!_surf)
        return;
    
    cv::Vec3f p, n;
    scene2vol(p, n, _surf, _surf_name, _surf_col, scene_loc, _vis_center, _scale);

    //for PlaneSurface we work with absolute coordinates only
    if (dynamic_cast<PlaneSurface*>(_surf))
        sendVolumeClicked(p, n, _surf, buttons, modifiers);
    //FIXME quite some assumptions ...
    else if (_surf_name == "segmentation")
        sendVolumeClicked(p, n, _surf_col->surface("segmentation"), buttons, modifiers);
    // sendVolumeClicked(p, n, _surf_col->surface("visible_segmentation"), buttons, modifiers);
    else
        std::cout << "FIXME: onVolumeClicked()" << std::endl;
}

void CVolumeViewer::setCache(ChunkCache *cache_)
{
    cache = cache_;
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
        PlaneSurface *plane = dynamic_cast<PlaneSurface*>(_surf);
        
        if (!plane)
            return;
        
        fGraphicsView->centerOn(0,0);
        
        if (poi->p == plane->origin())
            return;
        
        plane->setOrigin(poi->p);
        
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
    if (_surf_name == "segmentation" && _composite_enabled && _composite_layers > 1) {
        // Composite rendering for segmentation view
        cv::Mat_<float> accumulator;
        int count = 0;
        
        int half_range = (_composite_layers - 1) / 2;
        
        for (int z = -half_range; z <= half_range; z++) {
            cv::Mat_<cv::Vec3f> slice_coords;
            cv::Mat_<uint8_t> slice_img;
            
            cv::Vec2f roi_c = {roi.x+roi.width/2, roi.y + roi.height/2};
            _ptr = _surf->pointer();
            cv::Vec3f diff = {roi_c[0],roi_c[1],0};
            _surf->move(_ptr, diff/_scale);
            _vis_center = roi_c;
            _surf->gen(&slice_coords, nullptr, roi.size(), _ptr, _scale, {-roi.width/2, -roi.height/2, _z_off + z});
            
            readInterpolated3D(slice_img, volume->zarrDataset(_ds_sd_idx), slice_coords*_ds_scale, cache);
            
            // Convert to float for accumulation
            cv::Mat_<float> slice_float;
            slice_img.convertTo(slice_float, CV_32F);
            
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
    
    renderPoints();
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
            SurfacePointer *ptrs[omp_get_max_threads()] = {};
            
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
    for(auto &col : _intersect_items)
        for(auto &item : col.second)
            item->setVisible(true);

    renderVisible();
    
    if (dynamic_cast<PlaneSurface*>(_surf))
        renderIntersections();
}

void CVolumeViewer::onPanStart(Qt::MouseButton buttons, Qt::KeyboardModifiers modifiers)
{
    renderVisible();
    
    for(auto &col : _intersect_items)
        for(auto &item : col.second)
            item->setVisible(false);
    
    if (dynamic_cast<PlaneSurface*>(_surf))
        invalidateIntersect();
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

void CVolumeViewer::renderPoints()
{
    for(auto &item : _points_items) {
        // Only remove item if it's actually in our scene
        if (item && item->scene() == fScene) {
            fScene->removeItem(item);
        }
        delete item;
    }
    _points_items.resize(0);
    
    std::vector<cv::Vec3f> all_ps(_red_points);
    all_ps.insert(all_ps.end(), _blue_points.begin(), _blue_points.end());
    
    int n = -1;
    for(auto &wp : all_ps) {
        n++;
        PlaneSurface *plane = dynamic_cast<PlaneSurface*>(_surf);
        QuadSurface *quad = dynamic_cast<QuadSurface*>(_surf);
        
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
        
        QColor col = QColor(100, 100, 255);
        if (n < _red_points.size())
            col = QColor(255, 100, 100);
        
        QGraphicsItem *item = fScene->addEllipse(p[0]-4, p[1]-4, 8, 8, QPen(Qt::white), QBrush(col, Qt::SolidPattern));
        item->setZValue(30);
        _points_items.push_back(item);
    }
}


void CVolumeViewer::onPointsChanged(const std::vector<cv::Vec3f> red, const std::vector<cv::Vec3f> blue)
{
    _red_points = red;
    _blue_points = blue;
    
    renderPoints();
}

void CVolumeViewer::onPathsChanged(const QList<PathData>& paths)
{
    _paths = paths;
    renderPaths();
}

void CVolumeViewer::onMousePress(QPointF scene_loc, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
    if (!_surf) {
        return;
    }
    
    cv::Vec3f p, n;
    scene2vol(p, n, _surf, _surf_name, _surf_col, scene_loc, _vis_center, _scale);
    
    emit sendMousePressVolume(p, button, modifiers);
}

void CVolumeViewer::onMouseMove(QPointF scene_loc, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers)
{
    if (!_surf) {
        return;
    }
    
    cv::Vec3f p, n;
    scene2vol(p, n, _surf, _surf_name, _surf_col, scene_loc, _vis_center, _scale);
    
    emit sendMouseMoveVolume(p, buttons, modifiers);
}

void CVolumeViewer::onMouseRelease(QPointF scene_loc, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
    if (!_surf) {
        return;
    }
    
    cv::Vec3f p, n;
    scene2vol(p, n, _surf, _surf_name, _surf_col, scene_loc, _vis_center, _scale);
    
    emit sendMouseReleaseVolume(p, button, modifiers);
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

void CVolumeViewer::setCompositeMethod(const std::string& method)
{
    if (method != _composite_method && (method == "max" || method == "mean" || method == "min")) {
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
