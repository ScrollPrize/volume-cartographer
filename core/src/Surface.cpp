#include "vc/core/util/Surface.hpp"

#include "vc/core/io/PointSetIO.hpp"
#include "vc/core/util/Slicing.hpp"
#include "vc/core/types/ChunkedTensor.hpp"

#include "SurfaceHelpers.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
//TODO remove
#include <opencv2/highgui.hpp>

#include <unordered_map>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

cv::Vec2f offsetPoint2d(TrivialSurfacePointer *ptr, const cv::Vec3f &offset)
{
    cv::Vec3f p = ptr->loc + offset;
    return {p[0], p[1]};
}

//NOTE we have 3 coordiante systems. Nominal (voxel volume) coordinates, internal relative (ptr) coords (where _center is at 0/0) and internal absolute (_points) coordinates where the upper left corner is at 0/0.
static cv::Vec3f internal_loc(const cv::Vec3f &nominal, const cv::Vec3f &internal, const cv::Vec2f &scale)
{
    return internal + cv::Vec3f(nominal[0]*scale[0], nominal[1]*scale[1], nominal[2]);
}

static cv::Vec3f nominal_loc(const cv::Vec3f &nominal, const cv::Vec3f &internal, const cv::Vec2f &scale)
{
    return nominal + cv::Vec3f(internal[0]/scale[0], internal[1]/scale[1], internal[2]);
}

Surface::~Surface()
{
    if (meta) {
        delete meta;
    }
}

PlaneSurface::PlaneSurface(cv::Vec3f origin_, cv::Vec3f normal) : _origin(origin_)
{
    cv::normalize(normal, _normal);
    update();
};

void PlaneSurface::setNormal(cv::Vec3f normal)
{
    cv::normalize(normal, _normal);
    update();
}

void PlaneSurface::setOrigin(cv::Vec3f origin)
{
    _origin = origin;
    update();
}

cv::Vec3f PlaneSurface::origin()
{
    return _origin;
}

float PlaneSurface::pointDist(cv::Vec3f wp)
{
    float plane_off = _origin.dot(_normal);
    float scalarp = wp.dot(_normal) - plane_off /*- _z_off*/;

    return abs(scalarp);
}

//given origin and normal, return the normalized vector v which describes a point : origin + v which lies in the plane and maximizes v.x at the cost of v.y,v.z
cv::Vec3f vx_from_orig_norm(const cv::Vec3f &o, const cv::Vec3f &n)
{
    //impossible
    if (n[1] == 0 && n[2] == 0)
        return {0,0,0};

    //also trivial
    if (n[0] == 0)
        return {1,0,0};

    cv::Vec3f v = {1,0,0};

    if (n[1] == 0) {
        v[1] = 0;
        //either n1 or n2 must be != 0, see first edge case
        v[2] = -n[0]/n[2];
        cv::normalize(v, v, 1,0, cv::NORM_L2);
        return v;
    }

    if (n[2] == 0) {
        //either n1 or n2 must be != 0, see first edge case
        v[1] = -n[0]/n[1];
        v[2] = 0;
        cv::normalize(v, v, 1,0, cv::NORM_L2);
        return v;
    }

    v[1] = -n[0]/(n[1]+n[2]);
    v[2] = v[1];
    cv::normalize(v, v, 1,0, cv::NORM_L2);

    return v;
}

cv::Vec3f vy_from_orig_norm(const cv::Vec3f &o, const cv::Vec3f &n)
{
    cv::Vec3f v = vx_from_orig_norm({o[1],o[0],o[2]}, {n[1],n[0],n[2]});
    return {v[1],v[0],v[2]};
}

static void vxy_from_normal(cv::Vec3f orig, cv::Vec3f normal, cv::Vec3f &vx, cv::Vec3f &vy)
{
    vx = vx_from_orig_norm(orig, normal);
    vy = vy_from_orig_norm(orig, normal);

    //TODO will there be a jump around the midpoint?
    if (abs(vx[0]) >= abs(vy[1]))
        vy = cv::Mat(normal).cross(cv::Mat(vx));
    else
        vx = cv::Mat(normal).cross(cv::Mat(vy));

    //FIXME probably not the right way to normalize the direction?
    if (vx[0] < 0)
        vx *= -1;
    if (vy[1] < 0)
        vy *= -1;
}

void PlaneSurface::update()
{
    cv::Vec3f vx, vy;

    vxy_from_normal(_origin,_normal,vx,vy);

    std::vector <cv::Vec3f> src = {_origin,_origin+_normal,_origin+vx,_origin+vy};
    std::vector <cv::Vec3f> tgt = {{0,0,0},{0,0,1},{1,0,0},{0,1,0}};
    cv::Mat transf;
    cv::Mat inliers;

    cv::estimateAffine3D(src, tgt, transf, inliers, 0.1, 0.99);

    _M = transf({0,0,3,3});
    _T = transf({3,0,1,3});
}

cv::Vec3f PlaneSurface::project(cv::Vec3f wp, float render_scale, float coord_scale)
{
    cv::Vec3d res = _M*cv::Vec3d(wp)+_T;
    res *= render_scale*coord_scale;

    return {res(0), res(1), res(2)};
}

float PlaneSurface::scalarp(cv::Vec3f point) const
{
    return point.dot(_normal) - _origin.dot(_normal);
}

void PlaneSurface::gen(cv::Mat_<cv::Vec3f> *coords, cv::Mat_<cv::Vec3f> *normals, cv::Size size, SurfacePointer *ptr, float scale, const cv::Vec3f &offset)
{
    TrivialSurfacePointer _ptr({0,0,0});
    if (!ptr)
        ptr = &_ptr;
    TrivialSurfacePointer *ptr_inst = dynamic_cast<TrivialSurfacePointer*>(ptr);

    bool create_normals = normals || offset[2] || ptr_inst->loc[2];

    cv::Vec3f total_offset = internal_loc(offset/scale, ptr_inst->loc, {1,1});
    // std::cout << "PlaneCoords::gen upper left" << upper_left_actual /*<< ptr_inst->loc*/ << origin << offset << scale << std::endl;

    int w = size.width;
    int h = size.height;

    cv::Mat_<cv::Vec3f> _coords_header;
    cv::Mat_<cv::Vec3f> _normals_header;

    if (!coords)
        coords = &_coords_header;
    if (!normals)
        normals = &_normals_header;

    coords->create(size);

    if (create_normals)
        normals->create(size);

    cv::Vec3f vx, vy;
    vxy_from_normal(_origin,_normal,vx,vy);

    float m = 1/scale;

    cv::Vec3f use_origin = _origin + _normal*total_offset[2];

#pragma omp parallel for
    for(int j=0;j<h;j++)
        for(int i=0;i<w;i++) {
            (*coords)(j,i) = vx*(i*m+total_offset[0]) + vy*(j*m+total_offset[1]) + use_origin;
        }
}

TrivialSurfacePointer *PlaneSurface::pointer()
{
    return new TrivialSurfacePointer({0,0,0});
}

void PlaneSurface::move(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    TrivialSurfacePointer *ptr_inst = dynamic_cast<TrivialSurfacePointer*>(ptr);
    assert(ptr_inst);

    ptr_inst->loc += offset;
}

cv::Vec3f PlaneSurface::loc(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    TrivialSurfacePointer *ptr_inst = dynamic_cast<TrivialSurfacePointer*>(ptr);
    assert(ptr_inst);

    return ptr_inst->loc+offset;
}

cv::Vec3f PlaneSurface::coord(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    cv::Mat_<cv::Vec3f> coords;

    gen(&coords, nullptr, {1,1}, ptr, 1.0, offset);

    return coords(0,0);
}

cv::Vec3f PlaneSurface::normal(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    return _normal;
}

QuadSurface::QuadSurface(const cv::Mat_<cv::Vec3f> &points, const cv::Vec2f &scale) : 
    QuadSurface(new cv::Mat_<cv::Vec3f>(points.clone()), scale)
{
}

QuadSurface::QuadSurface(cv::Mat_<cv::Vec3f> *points, const cv::Vec2f &scale)
{
    _points = points;
    //-1 as many times we read with linear interpolation and access +1 locations
    _bounds = {0,0,_points->cols-1,_points->rows-1};
    _scale = scale;
    _center = {_points->cols/2.0/_scale[0],_points->rows/2.0/_scale[1],0};
}

QuadSurface::~QuadSurface()
{
    if (_points) {
        delete _points;
    }
}

QuadSurface *smooth_vc_segmentation(QuadSurface *src)
{
    cv::Mat_<cv::Vec3f> points = smooth_vc_segmentation(src->rawPoints());
    
    double sx, sy;
    vc_segmentation_scales(points, sx, sy);
    
    return new QuadSurface(points, {sx,sy});
}

TrivialSurfacePointer *QuadSurface::pointer()
{
    return new TrivialSurfacePointer({0,0,0});
}

void QuadSurface::move(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    TrivialSurfacePointer *ptr_inst = dynamic_cast<TrivialSurfacePointer*>(ptr);
    assert(ptr_inst);
    
    ptr_inst->loc += cv::Vec3f(offset[0]*_scale[0],offset[1]*_scale[1],offset[2]);
}

bool QuadSurface::valid(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    TrivialSurfacePointer *ptr_inst = dynamic_cast<TrivialSurfacePointer*>(ptr);
    assert(ptr_inst);
    cv::Vec3f p = internal_loc(offset+_center, ptr_inst->loc, _scale);
    
    return _bounds.contains(cv::Point(p[0],p[1]));
}

cv::Vec3f QuadSurface::coord(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    TrivialSurfacePointer *ptr_inst = dynamic_cast<TrivialSurfacePointer*>(ptr);
    assert(ptr_inst);
    cv::Vec3f p = internal_loc(offset+_center, ptr_inst->loc, _scale);

    cv::Rect bounds = {0,0,_points->cols-2,_points->rows-2};
    if (!bounds.contains(cv::Point(p[0],p[1])))
        return {-1,-1,-1};
        
    return at_int((*_points), {p[0],p[1]});
}

cv::Vec3f QuadSurface::loc(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    TrivialSurfacePointer *ptr_inst = dynamic_cast<TrivialSurfacePointer*>(ptr);
    assert(ptr_inst);
    
    return nominal_loc(offset, ptr_inst->loc, _scale);
}

cv::Vec3f QuadSurface::loc_raw(SurfacePointer *ptr)
{
    TrivialSurfacePointer *ptr_inst = dynamic_cast<TrivialSurfacePointer*>(ptr);
    assert(ptr_inst);

    return internal_loc(_center, ptr_inst->loc, _scale);
}

cv::Vec3f QuadSurface::normal(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    TrivialSurfacePointer *ptr_inst = dynamic_cast<TrivialSurfacePointer*>(ptr);
    assert(ptr_inst);
    cv::Vec3f p = internal_loc(offset+_center, ptr_inst->loc, _scale);
    
    return grid_normal((*_points), p);
}

static inline float sdist(const cv::Vec3f &a, const cv::Vec3f &b)
{
    cv::Vec3f d = a-b;
    // return d.dot(d);
    return d[0]*d[0] + d[1]*d[1] + d[2]*d[2];
}

static inline cv::Vec2f mul(const cv::Vec2f &a, const cv::Vec2f &b)
{
    return{a[0]*b[0],a[1]*b[1]};
}

static float tdist(const cv::Vec3f &a, const cv::Vec3f &b, float t_dist)
{
    cv::Vec3f d = a-b;
    float l = sqrt(d.dot(d));

    return abs(l-t_dist);
}

static float tdist_sum(const cv::Vec3f &v, const std::vector<cv::Vec3f> &tgts, const std::vector<float> &tds)
{
    float sum = 0;
    for(int i=0;i<tgts.size();i++) {
        float d = tdist(v, tgts[i], tds[i]);
        sum += d*d;
    }

    return sum;
}

//search location in points where we minimize error to multiple objectives using iterated local search
//tgts,tds -> distance to some POIs
//plane -> stay on plane
float min_loc(const cv::Mat_<cv::Vec3f> &points, cv::Vec2f &loc, cv::Vec3f &out, const std::vector<cv::Vec3f> &tgts, const std::vector<float> &tds, PlaneSurface *plane, float init_step, float min_step)
{
    if (!loc_valid(points, {loc[1],loc[0]})) {
        out = {-1,-1,-1};
        return -1;
    }

    bool changed = true;
    cv::Vec3f val = at_int(points, loc);
    out = val;
    float best = tdist_sum(val, tgts, tds);
    if (plane) {
        float d = plane->pointDist(val);
        best += d*d;
    }
    float res;

    // std::vector<cv::Vec2f> search = {{0,-1},{0,1},{-1,-1},{-1,0},{-1,1},{1,-1},{1,0},{1,1}};
    std::vector<cv::Vec2f> search = {{0,-1},{0,1},{-1,0},{1,0}};
    float step = init_step;



    while (changed) {
        changed = false;

        for(auto &off : search) {
            cv::Vec2f cand = loc+off*step;

            if (!loc_valid(points, {cand[1],cand[0]})) {
                // out = {-1,-1,-1};
                // loc = {-1,-1};
                // return -1;
                continue;
            }

            val = at_int(points, cand);
            // std::cout << "at" << cand << val << std::endl;
            res = tdist_sum(val, tgts, tds);
            if (plane) {
                float d = plane->pointDist(val);
                res += d*d;
            }
            if (res < best) {
                // std::cout << res << val << step << cand << "\n";
                changed = true;
                best = res;
                loc = cand;
                out = val;
            }
            // else
                // std::cout << "(" << res << val << step << cand << "\n";
        }

        if (changed)
            continue;

        step *= 0.5;
        changed = true;

        if (step < min_step)
            break;
    }

    // std::cout << "best" << best << out << "\n" <<  std::endl;
    return best;
}

template <typename E>
static float search_min_loc(const cv::Mat_<E> &points, cv::Vec2f &loc, cv::Vec3f &out, cv::Vec3f tgt, cv::Vec2f init_step, float min_step_x)
{
    cv::Rect boundary(1,1,points.cols-2,points.rows-2);
    if (!boundary.contains(cv::Point(loc))) {
        out = {-1,-1,-1};
        return -1;
    }
    
    bool changed = true;
    E val = at_int(points, loc);
    out = val;
    float best = sdist(val, tgt);
    float res;
    
    //TODO check maybe add more search patterns, compare motion estimatino for video compression, x264/x265, ...
    std::vector<cv::Vec2f> search = {{0,-1},{0,1},{-1,-1},{-1,0},{-1,1},{1,-1},{1,0},{1,1}};
    // std::vector<cv::Vec2f> search = {{0,-1},{0,1},{-1,0},{1,0}};
    cv::Vec2f step = init_step;
    
    while (changed) {
        changed = false;
        
        for(auto &off : search) {
            cv::Vec2f cand = loc+mul(off,step);
            
            //just skip if out of bounds
            if (!boundary.contains(cv::Point(cand)))
                continue;
            
            val = at_int(points, cand);
            res = sdist(val, tgt);
            if (res < best) {
                changed = true;
                best = res;
                loc = cand;
                out = val;
            }
        }
        
        if (changed)
            continue;
        
        step *= 0.5;
        changed = true;
        
        if (step[0] < min_step_x)
            break;
    }

    return sqrt(best);
}

static float dot_s(const cv::Vec3f &p)
{
    return p[0]*p[0] + p[1]*p[1] + p[2]*p[2];
}

template <typename E>
float ldist(const E &p, const cv::Vec3f &tgt_o, const cv::Vec3f &tgt_v)
{
    return cv::norm((p-tgt_o).cross(p-tgt_o-tgt_v))/cv::norm(tgt_v);
}

template <typename E>
static float search_min_line(const cv::Mat_<E> &points, cv::Vec2f &loc, cv::Vec3f &out, cv::Vec3f tgt_o, cv::Vec3f tgt_v, cv::Vec2f init_step, float min_step_x)
{
    cv::Rect boundary(1,1,points.cols-2,points.rows-2);
    if (!boundary.contains(cv::Point(loc))) {
        out = {-1,-1,-1};
        return -1;
    }
    
    bool changed = true;
    E val = at_int(points, loc);
    out = val;
    float best = ldist(val, tgt_o, tgt_v);
    float res;
    
    //TODO check maybe add more search patterns, compare motion estimatino for video compression, x264/x265, ...
    std::vector<cv::Vec2f> search = {{0,-1},{0,1},{-1,-1},{-1,0},{-1,1},{1,-1},{1,0},{1,1}};
    // std::vector<cv::Vec2f> search = {{0,-1},{0,1},{-1,0},{1,0}};
    cv::Vec2f step = init_step;
    
    while (changed) {
        changed = false;
        
        for(auto &off : search) {
            cv::Vec2f cand = loc+mul(off,step);
            
            //just skip if out of bounds
            if (!boundary.contains(cv::Point(cand)))
                continue;
                
                val = at_int(points, cand);
                res = ldist(val, tgt_o, tgt_v);
                if (res < best) {
                    changed = true;
                    best = res;
                    loc = cand;
                    out = val;
                }
        }
        
        if (changed)
            continue;
        
        step *= 0.5;
        changed = true;
        
        if (step[0] < min_step_x)
            break;
    }
    
    return best;
}

//search the surface point that is closest to th tgt coord
template <typename E>
float _pointTo(cv::Vec2f &loc, const cv::Mat_<E> &points, const cv::Vec3f &tgt, float th, int max_iters, float scale)
{
    loc = cv::Vec2f(points.cols/2,points.rows/2);
    cv::Vec3f _out;
    
    cv::Vec2f step_small = {std::max(1.0f,scale),std::max(1.0f,scale)};
    float min_mul = std::min(0.1*points.cols/scale,0.1*points.rows/scale);
    cv::Vec2f step_large = {min_mul*scale,min_mul*scale};
    
    float dist = search_min_loc(points, loc, _out, tgt, step_small, scale*0.1);
    
    if (dist < th && dist >= 0) {
        return dist;
    }
    
    cv::Vec2f min_loc = loc;
    float min_dist = dist;
    if (min_dist < 0)
        min_dist = 10*(points.cols/scale+points.rows/scale);
    
    //FIXME is this excessive?
    int r_full = 0;
    for(int r=0;r<10*max_iters && r_full < max_iters;r++) {
        //FIXME skipn invalid init locs!
        loc = {1 + (rand() % points.cols-3), 1 + (rand() % points.rows-3)};
        
        if (points(loc[1],loc[0])[0] == -1)
            continue;
        
        r_full++;
        
        float dist = search_min_loc(points, loc, _out, tgt, step_large, scale*0.1);
        
        if (dist < th && dist >= 0) {
            dist = search_min_loc(points, loc, _out, tgt, step_small, scale*0.1);
            return dist;
        } else if (dist >= 0 && dist < min_dist) {
            min_loc = loc;
            min_dist = dist;
        }
    }

    loc = min_loc;
    return min_dist;
}

float pointTo(cv::Vec2f &loc, const cv::Mat_<cv::Vec3d> &points, const cv::Vec3f &tgt, float th, int max_iters, float scale)
{
    return _pointTo(loc, points, tgt, th, max_iters, scale);
}

float pointTo(cv::Vec2f &loc, const cv::Mat_<cv::Vec3f> &points, const cv::Vec3f &tgt, float th, int max_iters, float scale)
{
    return _pointTo(loc, points, tgt, th, max_iters, scale);
}

//search the surface point that is closest to th tgt coord
float QuadSurface::pointTo(SurfacePointer *ptr, const cv::Vec3f &tgt, float th, int max_iters)
{
    TrivialSurfacePointer *tgt_ptr = dynamic_cast<TrivialSurfacePointer*>(ptr);
    assert(tgt_ptr);

    cv::Vec2f loc = cv::Vec2f(tgt_ptr->loc[0],tgt_ptr->loc[1]) + cv::Vec2f(_center[0]*_scale[0],_center[1]*_scale[1]);
    cv::Vec3f _out;
    
    cv::Vec2f step_small = {std::max(1.0f,_scale[0]),std::max(1.0f,_scale[1])};
    float min_mul = std::min(0.1*_points->cols/_scale[0],0.1*_points->rows/_scale[1]);
    cv::Vec2f step_large = {min_mul*_scale[0],min_mul*_scale[1]};

    float dist = search_min_loc(*_points, loc, _out, tgt, step_small, _scale[0]*0.1);
    
    if (dist < th && dist >= 0) {
        tgt_ptr->loc = cv::Vec3f(loc[0],loc[1],0) - cv::Vec3f(_center[0]*_scale[0],_center[1]*_scale[1],0);
        return dist;
    }
    
    cv::Vec2f min_loc = loc;
    float min_dist = dist;
    if (min_dist < 0)
        min_dist = 10*(_points->cols/_scale[0]+_points->rows/_scale[1]);
    
    int r_full = 0;
    for(int r=0;r<10*max_iters && r_full < max_iters;r++) {
        loc = {1 + (rand() % (_points->cols-3)), 1 + (rand() % (_points->rows-3))};
        
        if ((*_points)(loc[1],loc[0])[0] == -1)
            continue;
        
        r_full++;

        float dist = search_min_loc(*_points, loc, _out, tgt, step_large, _scale[0]*0.1);
        
        if (dist < th && dist >= 0) {
            dist = search_min_loc((*_points), loc, _out, tgt, step_small, _scale[0]*0.1);
            tgt_ptr->loc = cv::Vec3f(loc[0],loc[1],0) - cv::Vec3f(_center[0]*_scale[0],_center[1]*_scale[1],0);
            return dist;
        } else if (dist >= 0 && dist < min_dist) {
            min_loc = loc;
            min_dist = dist;
        }
    }
    
    tgt_ptr->loc = cv::Vec3f(min_loc[0],min_loc[1],0) - cv::Vec3f(_center[0]*_scale[0],_center[1]*_scale[1],0);
    return min_dist;
}

QuadSurface *load_quad_from_vcps(const std::string &path)
{    
    volcart::OrderedPointSet<cv::Vec3d> segment_raw = volcart::PointSetIO<cv::Vec3d>::ReadOrderedPointSet(path);
    
    cv::Mat src(segment_raw.height(), segment_raw.width(), CV_64FC3, (void*)const_cast<cv::Vec3d*>(&segment_raw[0]));
    cv::Mat_<cv::Vec3f> points;
    src.convertTo(points, CV_32F);    
    
    double sx, sy;
    
    vc_segmentation_scales(points, sx, sy);
    
    return new QuadSurface(points, {sx,sy});
}

bool face_contains_vertex(cv::Vec3i face, int vertex)
{
    if (face[0] == vertex)
        return true;
    if (face[1] == vertex)
        return true;
    if (face[2] == vertex)
        return true;
    return false;
}

//load quad surface from OBJ file using texture coordinates to determine grid structure
QuadSurface *load_quad_from_obj(const std::string &path)
{
    // vertex ID to 3d location (1-indexed in OBJ)
    std::unordered_map<int,cv::Vec3f> vertices;
    // texture coordinate ID to UV (1-indexed in OBJ)
    std::unordered_map<int,cv::Vec2f> texcoords;
    // Store faces with vertex and texture coordinate indices
    struct Face {
        std::vector<int> v_indices;
        std::vector<int> vt_indices;
    };
    std::vector<Face> faces;

    std::ifstream obj(path);
    if (!obj.is_open()) {
        std::cerr << "Cannot open OBJ file: " << path << std::endl;
        return nullptr;
    }

    std::string line;
    while (std::getline(obj, line))
    {
        if (line.empty())
            continue;
        
        if (line[0] == 'v' && (line.size() < 2 || line[1] == ' ')) {
            // Parse vertex
            std::istringstream iss(line);
            float x, y, z;
            char v;
            if (!(iss >> v >> x >> y >> z)) {
                continue;
            }
            vertices[vertices.size() + 1] = {x,y,z}; // OBJ uses 1-based indexing
        }
        else if (line[0] == 'v' && line[1] == 't' && (line.size() < 3 || line[2] == ' ')) {
            // Parse texture coordinate
            std::istringstream iss(line);
            float u, v;
            char vt1, vt2;
            if (!(iss >> vt1 >> vt2 >> u >> v)) {
                continue;
            }
            texcoords[texcoords.size() + 1] = {u,v}; // OBJ uses 1-based indexing
        }
        else if (line[0] == 'f' && (line.size() < 2 || line[1] == ' ')) {
            // Parse face
            std::istringstream iss(line.substr(2));
            Face face;
            std::string vertex_str;
            
            while (iss >> vertex_str) {
                // Handle f v/vt/vn or f v/vt format
                std::vector<std::string> parts;
                size_t pos = 0;
                while ((pos = vertex_str.find('/')) != std::string::npos) {
                    parts.push_back(vertex_str.substr(0, pos));
                    vertex_str.erase(0, pos + 1);
                }
                parts.push_back(vertex_str);
                
                if (parts.size() >= 2) {
                    face.v_indices.push_back(std::stoi(parts[0]));
                    if (!parts[1].empty()) {
                        face.vt_indices.push_back(std::stoi(parts[1]));
                    }
                } else if (parts.size() == 1) {
                    face.v_indices.push_back(std::stoi(parts[0]));
                }
            }
            
            if (!face.v_indices.empty()) {
                faces.push_back(face);
            }
        }
    }
    obj.close();

    if (vertices.empty() || texcoords.empty() || faces.empty()) {
        std::cerr << "Missing vertices, texture coordinates, or faces in OBJ file" << std::endl;
        return nullptr;
    }

    // Infer grid dimensions from texture coordinates
    float min_u = 1.0f, max_u = 0.0f;
    float min_v = 1.0f, max_v = 0.0f;
    std::set<float> unique_u, unique_v;
    
    for (const auto& [idx, uv] : texcoords) {
        min_u = std::min(min_u, uv[0]);
        max_u = std::max(max_u, uv[0]);
        min_v = std::min(min_v, uv[1]);
        max_v = std::max(max_v, uv[1]);
        unique_u.insert(uv[0]);
        unique_v.insert(uv[1]);
    }

    // Try to determine grid dimensions from unique UV values
    int width = unique_u.size();
    int height = unique_v.size();
    
    // If we have too many unique values, try a different approach
    if (width * height > vertices.size() * 2) {
        // Estimate based on assuming normalized UVs with regular spacing
        // Find minimum spacing between unique values
        float min_u_spacing = 1.0f;
        float min_v_spacing = 1.0f;
        
        auto u_it = unique_u.begin();
        if (unique_u.size() > 1) {
            float prev_u = *u_it++;
            for (; u_it != unique_u.end(); ++u_it) {
                float spacing = *u_it - prev_u;
                if (spacing > 0.0001f) {
                    min_u_spacing = std::min(min_u_spacing, spacing);
                }
                prev_u = *u_it;
            }
        }
        
        auto v_it = unique_v.begin();
        if (unique_v.size() > 1) {
            float prev_v = *v_it++;
            for (; v_it != unique_v.end(); ++v_it) {
                float spacing = *v_it - prev_v;
                if (spacing > 0.0001f) {
                    min_v_spacing = std::min(min_v_spacing, spacing);
                }
                prev_v = *v_it;
            }
        }
        
        width = std::round((max_u - min_u) / min_u_spacing) + 1;
        height = std::round((max_v - min_v) / min_v_spacing) + 1;
    }

    std::cout << "Inferred grid dimensions: " << width << "x" << height << std::endl;
    std::cout << "UV range: u[" << min_u << ", " << max_u << "], v[" << min_v << ", " << max_v << "]" << std::endl;

    // Create the points matrix
    cv::Mat_<cv::Vec3f>* points = new cv::Mat_<cv::Vec3f>(height, width, cv::Vec3f(-1, -1, -1));

    // Map vertices to grid positions using texture coordinates
    std::set<cv::Vec2i, std::function<bool(const cv::Vec2i&, const cv::Vec2i&)>> used_cells(
        [](const cv::Vec2i& a, const cv::Vec2i& b) {
            return a[1] < b[1] || (a[1] == b[1] && a[0] < b[0]);
        }
    );
    
    for (const auto& face : faces) {
        for (size_t i = 0; i < face.v_indices.size() && i < face.vt_indices.size(); i++) {
            int v_idx = face.v_indices[i];
            int vt_idx = face.vt_indices[i];
            
            if (vertices.find(v_idx) != vertices.end() && texcoords.find(vt_idx) != texcoords.end()) {
                cv::Vec3f vertex = vertices[v_idx];
                cv::Vec2f uv = texcoords[vt_idx];
                
                // Map UV to grid position
                int grid_x = std::round((uv[0] - min_u) / (max_u - min_u) * (width - 1));
                int grid_y = std::round((uv[1] - min_v) / (max_v - min_v) * (height - 1));
                
                // Clamp to valid range
                grid_x = std::max(0, std::min(width - 1, grid_x));
                grid_y = std::max(0, std::min(height - 1, grid_y));
                
                cv::Vec2i cell(grid_x, grid_y);
                
                // Only set if this cell hasn't been used yet (taking the first)
                if (used_cells.find(cell) == used_cells.end()) {
                    (*points)(grid_y, grid_x) = vertex;
                    used_cells.insert(cell);
                }
            }
        }
    }

    // Calculate scale based on average edge lengths
    double sx = 1.0, sy = 1.0;
    int count_x = 0, count_y = 0;
    double sum_x = 0.0, sum_y = 0.0;
    
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width - 1; i++) {
            if ((*points)(j, i)[0] != -1 && (*points)(j, i + 1)[0] != -1) {
                sum_x += cv::norm((*points)(j, i) - (*points)(j, i + 1));
                count_x++;
            }
        }
    }
    
    for (int j = 0; j < height - 1; j++) {
        for (int i = 0; i < width; i++) {
            if ((*points)(j, i)[0] != -1 && (*points)(j + 1, i)[0] != -1) {
                sum_y += cv::norm((*points)(j, i) - (*points)(j + 1, i));
                count_y++;
            }
        }
    }
    
    if (count_x > 0) sx = sum_x / count_x;
    if (count_y > 0) sy = sum_y / count_y;
    
    std::cout << "Calculated scale: sx=" << sx << ", sy=" << sy << std::endl;
    std::cout << "Grid fill rate: " << used_cells.size() << "/" << (width * height) << " cells" << std::endl;
    
    return new QuadSurface(points, {static_cast<float>(sx), static_cast<float>(sy)});
}

void QuadSurface::gen(cv::Mat_<cv::Vec3f> *coords, cv::Mat_<cv::Vec3f> *normals, cv::Size size, SurfacePointer *ptr, float scale, const cv::Vec3f &offset)
{
    TrivialSurfacePointer _ptr({0,0,0});
    if (!ptr)
        ptr = &_ptr;
    TrivialSurfacePointer *ptr_inst = dynamic_cast<TrivialSurfacePointer*>(ptr);
    
    bool create_normals = normals || offset[2] || ptr_inst->loc[2];
    
    cv::Vec3f upper_left_actual = internal_loc(offset/scale+_center, ptr_inst->loc, _scale);
    
    int w = size.width;
    int h = size.height;

    cv::Mat_<cv::Vec3f> _coords_header;
    cv::Mat_<cv::Vec3f> _normals_header;
    
    if (!coords)
        coords = &_coords_header;
    if (!normals)
        normals = &_normals_header;

    coords->create(size+cv::Size(8,8));
    
    std::vector<cv::Vec2f> dst = {{0,0},{w+8,0},{0,h+8}};
    cv::Vec2f off2d = {upper_left_actual[0]-4*_scale[0]/scale,upper_left_actual[1]-4*_scale[1]/scale};
    std::vector<cv::Vec2f> src = {off2d,off2d+cv::Vec2f((w+8)*_scale[0]/scale,0),off2d+cv::Vec2f(0,(h+8)*_scale[1]/scale)};
    
    cv::Mat affine = cv::getAffineTransform(src, dst);
    cv::warpAffine(*_points, *coords, affine, size+cv::Size(8,8));
    
    //TODO create normals directly from input points instead off on sampled output ...
    if (create_normals) {
        normals->create(size);
        for(int j=0;j<h;j++)
            for(int i=0;i<w;i++)
                (*normals)(j, i) = grid_normal(*coords, {i+4,j+4});

        *coords = (*coords)(cv::Rect(4,4,size.width,size.height)).clone();

        if (upper_left_actual[2])
            *coords += (*normals)*upper_left_actual[2];
    }
    else
        *coords = (*coords)(cv::Rect(4,4,size.width,size.height)).clone();
}

SurfaceControlPoint::SurfaceControlPoint(Surface *base, SurfacePointer *ptr_, const cv::Vec3f &control)
{
    ptr = ptr_->clone();
    orig_wp = base->coord(ptr_);
    normal = base->normal(ptr_);
    control_point = control;
}

DeltaSurface::DeltaSurface(Surface *base) : _base(base)
{
    
}

void DeltaSurface::setBase(Surface *base)
{
    _base = base;
}

SurfacePointer *DeltaSurface::pointer()
{
    return _base->pointer();
}

void DeltaSurface::move(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    _base->move(ptr, offset);
}

bool DeltaSurface::valid(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    return _base->valid(ptr, offset);
}

cv::Vec3f DeltaSurface::loc(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    return _base->loc(ptr, offset);
}

cv::Vec3f DeltaSurface::coord(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    return _base->coord(ptr, offset);
}

cv::Vec3f DeltaSurface::normal(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    return _base->normal(ptr, offset);
}

float DeltaSurface::pointTo(SurfacePointer *ptr, const cv::Vec3f &tgt, float th, int max_iters)
{
    return _base->pointTo(ptr, tgt, th, max_iters);
}


void ControlPointSurface::addControlPoint(SurfacePointer *base_ptr, cv::Vec3f control_point)
{
    _controls.push_back(SurfaceControlPoint(this, base_ptr, control_point));
    
}

void ControlPointSurface::gen(cv::Mat_<cv::Vec3f> *coords_, cv::Mat_<cv::Vec3f> *normals_, cv::Size size, SurfacePointer *ptr, float scale, const cv::Vec3f &offset)
{
    std::cout << "corr gen " << _controls.size() << std::endl;
    cv::Mat_<cv::Vec3f> _coords_local;
    
    cv::Mat_<cv::Vec3f> *coords = coords_;
    
    if (!coords)
        coords = &_coords_local;
    
    TrivialSurfacePointer _ptr_local({0,0,0});
    if (!ptr)
        ptr = &_ptr_local;
    
    TrivialSurfacePointer *ptr_inst = dynamic_cast<TrivialSurfacePointer*>(ptr);
    assert(ptr_inst);

    _base->gen(coords, normals_, size, ptr, scale, offset);
    
    int w = size.width;
    int h = size.height;
    cv::Rect bounds(0,0,w,h);
    
    //FIXME can we do this without assuming quad base? I think so ...
    
    cv::Vec3f upper_left_nominal = nominal_loc(offset/scale, ptr_inst->loc, dynamic_cast<QuadSurface*>(_base)->_scale);
    
    float z_offset = upper_left_nominal[2];
    upper_left_nominal[2] = 0;
    
    //FIXME implement z_offset
    
    for(auto p : _controls) {
        cv::Vec3f p_loc = nominal_loc(loc(p.ptr), ptr_inst->loc, dynamic_cast<QuadSurface*>(_base)->_scale)  - upper_left_nominal;
        std::cout << p_loc << p_loc*scale <<  loc(p.ptr) << ptr_inst->loc << std::endl;
        p_loc *= scale;
        cv::Rect roi(p_loc[0]-40,p_loc[1]-40,80,80);
        cv::Rect area = roi & bounds;
        
        PlaneSurface plane(p.control_point, p.normal);
        float delta = plane.scalarp(coord(p.ptr));
        cv::Vec3f move = delta*p.normal;
        
        std::cout << area << roi << bounds << move << p.control_point << p.normal << coord(p.ptr) << std::endl;
        
        for(int j=area.y;j<area.y+area.height;j++)
            for(int i=area.x;i<area.x+area.width;i++) {
                //TODO correct by scale!
                float w = sdist(p_loc, cv::Vec3f(i,j,0));
                w = exp(-w/(20*20));
                (*coords)(j,i) += w*move;
            }
    }
}

void ControlPointSurface::setBase(Surface *base)
{
    DeltaSurface::setBase(base);
    
    assert(dynamic_cast<QuadSurface*>(base));
    
    //FIXME reset control points?
    std::cout << "ERROR implement search for ControlPointSurface::setBase()" << std::endl;
}

RefineCompSurface::RefineCompSurface(z5::Dataset *ds, ChunkCache *cache, QuadSurface *base)
: DeltaSurface(base)
{
    _ds = ds;
    _cache = cache;
}

void RefineCompSurface::gen(cv::Mat_<cv::Vec3f> *coords_, cv::Mat_<cv::Vec3f> *normals_, cv::Size size, SurfacePointer *ptr, float scale, const cv::Vec3f &offset)
{
    cv::Mat_<cv::Vec3f> _coords_local;
    cv::Mat_<cv::Vec3f> _normals_local;
    
    cv::Mat_<cv::Vec3f> *coords = coords_;
    cv::Mat_<cv::Vec3f> *normals = normals_;
    
    if (!coords)
        coords = &_coords_local;
    if (!normals)
    normals = &_normals_local;
    
    TrivialSurfacePointer _ptr_local({0,0,0});
    if (!ptr)
        ptr = &_ptr_local;
    
    TrivialSurfacePointer *ptr_inst = dynamic_cast<TrivialSurfacePointer*>(ptr);
    assert(ptr_inst);
    
    _base->gen(coords, normals, size, ptr, scale, offset);
    
    cv::Mat_<cv::Vec3f> res;
    
    // cv::Mat_<float> integ(size, 0);
    // cv::Mat_<float> integ_blur(size, 0);
    cv::Mat_<float> transparent(size, 1);
    cv::Mat_<float> blur(size, 0);
    cv::Mat_<float> integ_z(size, 0);

    if (stop < start)
        step = -abs(step);

    for(int n=0;n<=(stop-start)/step;n++) {
        cv::Mat_<uint8_t> slice;
        float off = start + step*n;
        readInterpolated3D(slice, _ds, (*coords+*normals*off)*scale, _cache);
        
        cv::Mat floatslice;
        slice.convertTo(floatslice, CV_32F, 1/255.0);
        
        cv::GaussianBlur(floatslice, blur, {7,7}, 0);
        cv::Mat opaq_slice = blur;
        
        opaq_slice = (opaq_slice-low)/(high-low);
        opaq_slice = cv::min(opaq_slice,1);
        opaq_slice = cv::max(opaq_slice,0);
        
        cv::Mat joint = transparent.mul(opaq_slice);
        // integ += joint.mul(floatslice);
        // integ_blur += joint.mul(blur);
        integ_z += joint * off * scale;
        transparent = transparent-joint;
    }
    
    integ_z /= (1-transparent);
    
    //NOTE could be used as an additional output layer to improv visualization!
    // integ /= (1-transparent);
    // integ_blur /= (1-transparent);
    // cv::imwrite("blended_comp1.tif", integ/(integ_blur+0.5));

    cv::Mat mul;
    cv::cvtColor(integ_z, mul, cv::COLOR_GRAY2BGR);
    *coords += (*normals).mul(mul+1+offset[2]);
}

//TODO check if this actually works?!
void set_block(cv::Mat_<uint8_t> &block, const cv::Vec3f &last_loc, const cv::Vec3f &loc, const cv::Rect &roi, float step)
{
    int x1 = (loc[0]-roi.x)/step;
    int y1 = (loc[1]-roi.y)/step;
    int x2 = (last_loc[0]-roi.x)/step;
    int y2 = (last_loc[1]-roi.y)/step;

    if (x1 < 0 || y1 < 0 || x1 >= block.cols || y1 >= block.rows)
        return;
    if (x2 < 0 || y2 < 0 || x2 >= block.cols || y2 >= block.rows)
        return;

    if (x1 == x2 && y1 == y2)
        block(y1, x1) = 1;
    else
        cv::line(block, {x1,y1},{x2,y2}, 3);
}

uint8_t get_block(const cv::Mat_<uint8_t> &block, const cv::Vec3f &loc, const cv::Rect &roi, float step)
{
    int x = (loc[0]-roi.x)/step;
    int y = (loc[1]-roi.y)/step;

    if (x < 0 || y < 0 || x >= block.cols || y >= block.rows)
        return 1;

    return block(y, x);
}

template<typename T, int C>
//l is [y, x]!
bool area_valid(const cv::Mat_<cv::Vec<T,C>> &m, cv::Vec2f l)
{
    if (l[0] == -1)
        return false;

    cv::Rect bounds = {1, 1, m.rows-3,m.cols-3};
    cv::Vec2i li = {floor(l[0]),floor(l[1])};

    if (!bounds.contains(cv::Point(li)))
        return false;

    if (m(li[0],li[1])[0] == -1)
        return false;
    if (m(li[0]+1,li[1])[0] == -1)
        return false;
    if (m(li[0],li[1]+1)[0] == -1)
        return false;
    if (m(li[0]+1,li[1]+1)[0] == -1)
        return false;

    l -= cv::Vec2f(1,1);

    if (m(li[0],li[1])[0] == -1)
        return false;
    if (m(li[0]+3,li[1])[0] == -1)
        return false;
    if (m(li[0],li[1]+3)[0] == -1)
        return false;
    if (m(li[0]+3,li[1]+3)[0] == -1)
        return false;

    return true;
}

void find_intersect_segments(std::vector<std::vector<cv::Vec3f>> &seg_vol, std::vector<std::vector<cv::Vec2f>> &seg_grid, const cv::Mat_<cv::Vec3f> &points, PlaneSurface *plane, const cv::Rect &plane_roi, float step, int min_tries)
{
    //start with random points and search for a plane intersection

    float block_step = 0.5*step;

    cv::Mat_<uint8_t> block(cv::Size(plane_roi.width/block_step, plane_roi.height/block_step), 0);

    cv::Rect grid_bounds(1,1,points.cols-2,points.rows-2);

    std::vector<std::vector<cv::Vec3f>> seg_vol_raw;
    std::vector<std::vector<cv::Vec2f>> seg_grid_raw;

    for(int r=0;r<std::max(min_tries, std::max(points.cols,points.rows)/100);r++) {
        std::vector<cv::Vec3f> seg;
        std::vector<cv::Vec2f> seg_loc;
        std::vector<cv::Vec3f> seg2;
        std::vector<cv::Vec2f> seg_loc2;
        cv::Vec2f loc;
        cv::Vec2f loc2;
        cv::Vec2f loc3;
        cv::Vec3f point;
        cv::Vec3f point2;
        cv::Vec3f point3;
        cv::Vec3f plane_loc;
        cv::Vec3f last_plane_loc;
        float dist = -1;


        //initial points
        for(int i=0;i<std::max(min_tries, std::max(points.cols,points.rows)/100);i++) {
            loc = {std::rand() % (points.cols-1), std::rand() % (points.rows-1)};
            point = at_int(points, loc);

            plane_loc = plane->project(point);
            if (!plane_roi.contains(cv::Point(plane_loc[0],plane_loc[1])))
                continue;

                dist = min_loc(points, loc, point, {}, {}, plane, std::min(points.cols,points.rows)*0.1, 0.01);

                plane_loc = plane->project(point);
                if (!plane_roi.contains(cv::Point(plane_loc[0],plane_loc[1])))
                    dist = -1;

                if (get_block(block, plane_loc, plane_roi, block_step))
                    dist = -1;

            if (dist >= 0 && dist <= 1 || !loc_valid_xy(points, loc))
                break;
        }


        if (dist < 0 || dist > 1)
            continue;

        seg.push_back(point);
        seg_loc.push_back(loc);

        //point2
        loc2 = loc;
        //search point at distance of 1 to init point
        dist = min_loc(points, loc2, point2, {point}, {1}, plane, 0.01, 0.0001);

        if (dist < 0 || dist > 1 || !loc_valid_xy(points, loc))
            continue;

        seg.push_back(point2);
        seg_loc.push_back(loc2);

        last_plane_loc = plane->project(point);
        plane_loc = plane->project(point2);
        set_block(block, last_plane_loc, plane_loc, plane_roi, block_step);
        last_plane_loc = plane_loc;

        //go one direction
        for(int n=0;n<100;n++) {
            //now search following points
            cv::Vec2f loc3 = loc2+loc2-loc;

            if (!grid_bounds.contains(cv::Point(loc3)))
                break;

                point3 = at_int(points, loc3);

                //search point close to prediction + dist 1 to last point
                dist = min_loc(points, loc3, point3, {point,point2,point3}, {2*step,step,0}, plane, 0.01, 0.0001);

                //then refine
                dist = min_loc(points, loc3, point3, {point2}, {step}, plane, 0.01, 0.0001);

                if (dist < 0 || dist > 1 || !loc_valid_xy(points, loc))
                    break;

            seg.push_back(point3);
            seg_loc.push_back(loc3);
            point = point2;
            point2 = point3;
            loc = loc2;
            loc2 = loc3;

            plane_loc = plane->project(point3);
            if (get_block(block, plane_loc, plane_roi, block_step))
                break;

            set_block(block, last_plane_loc, plane_loc, plane_roi, block_step);
            last_plane_loc = plane_loc;
        }

        //now the other direction
        loc2 = seg_loc[0];
        loc = seg_loc[1];
        point2 = seg[0];
        point = seg[1];

        last_plane_loc = plane->project(point2);

        //FIXME repeat by not copying code ...
        for(int n=0;n<100;n++) {
            //now search following points
            cv::Vec2f loc3 = loc2+loc2-loc;

            if (!grid_bounds.contains(cv::Point(loc3[0])))
                break;

                point3 = at_int(points, loc3);

                //search point close to prediction + dist 1 to last point
                dist = min_loc(points, loc3, point3, {point,point2,point3}, {2*step,step,0}, plane, 0.01, 0.0001);

                //then refine
                dist = min_loc(points, loc3, point3, {point2}, {step}, plane, 0.01, 0.0001);

                if (dist < 0 || dist > 1 || !loc_valid_xy(points, loc))
                    break;

            seg2.push_back(point3);
            seg_loc2.push_back(loc3);
            point = point2;
            point2 = point3;
            loc = loc2;
            loc2 = loc3;

            plane_loc = plane->project(point3);
            if (get_block(block, plane_loc, plane_roi, block_step))
                break;

            set_block(block, last_plane_loc, plane_loc, plane_roi, block_step);
            last_plane_loc = plane_loc;
        }

        std::reverse(seg2.begin(), seg2.end());
        std::reverse(seg_loc2.begin(), seg_loc2.end());

        seg2.insert(seg2.end(), seg.begin(), seg.end());
        seg_loc2.insert(seg_loc2.end(), seg_loc.begin(), seg_loc.end());


        seg_vol_raw.push_back(seg2);
        seg_grid_raw.push_back(seg_loc2);
    }

    //split up into disconnected segments
    for(int s=0;s<seg_vol_raw.size();s++) {
        std::vector<cv::Vec3f> seg_vol_curr;
        std::vector<cv::Vec2f> seg_grid_curr;
        cv::Vec3f last = {-1,-1,-1};
        for(int n=0;n<seg_vol_raw[s].size();n++) {
                if (last[0] != -1 && cv::norm(last-seg_vol_raw[s][n]) >= 2*step) {
                seg_vol.push_back(seg_vol_curr);
                seg_grid.push_back(seg_grid_curr);
                seg_vol_curr.resize(0);
                seg_grid_curr.resize(0);
            }
            last = seg_vol_raw[s][n];
            seg_vol_curr.push_back(seg_vol_raw[s][n]);
            seg_grid_curr.push_back(seg_grid_raw[s][n]);
        }
        if (seg_vol_curr.size() >= 2) {
            seg_vol.push_back(seg_vol_curr);
            seg_grid.push_back(seg_grid_curr);
        }
    }
}

struct DSReader
{
    z5::Dataset *ds;
    float scale;
    ChunkCache *cache;
};

static float alphacomp_offset(DSReader &reader, cv::Vec3f point, cv::Vec3f normal, float start, float stop, float step)
{
    cv::Size size = {7,7};
    cv::Point2i c = {3,3};

    float transparent = 1;
    cv::Mat_<float> blur(size, 0);
    float integ_z = 0;

    cv::Mat_<cv::Vec3f> coords;
    PlaneSurface plane(point, normal);
    plane.gen(&coords, nullptr, size, nullptr, reader.scale, {0,0,0});

    coords *= reader.scale;
    float s = copysignf(1.0,step);

    for(double off=start;off*s<=stop*s;off+=step) {
        cv::Mat_<uint8_t> slice;
        //I hate opencv
        cv::Mat_<cv::Vec3f> offmat(size, normal*off*reader.scale);
        readInterpolated3D(slice, reader.ds, coords+offmat, reader.cache);

        cv::Mat floatslice;
        slice.convertTo(floatslice, CV_32F, 1/255.0);

        cv::GaussianBlur(floatslice, blur, {7,7}, 0);
        cv::Mat_<float> opaq_slice = blur;

        float low = 0.1; //map to 0
        float up = 1.0; //map to 1
        opaq_slice = (opaq_slice-low)/(up-low);
        opaq_slice = cv::min(opaq_slice,1);
        opaq_slice = cv::max(opaq_slice,0);

        float joint = transparent*opaq_slice(c);
        integ_z += joint * off;
        transparent = transparent-joint;
    }

    integ_z += transparent * stop;
    transparent = 0.0;

    // integ_z /= (1-transparent+1e-5);

    return integ_z;
}

float clampsigned(float val, float limit)
{
    if (val >= limit)
        return limit;
    if (val <= -limit)
        return -limit;

    return val;
}


void QuadSurface::save(std::filesystem::path &path_)
{
    if (path_.filename().empty())
        save(path_, path_.parent_path().filename());
    else
        save(path_, path_.filename());

}

void QuadSurface::save(const std::string &path_, const std::string &uuid)
{
    path = path_;
    
    if (!fs::create_directories(path)) {
        if (fs::exists(path))
            throw std::runtime_error("dir already exists => cannot run QuadSurface::save(): " + path.string());
        else
            throw std::runtime_error("error creating dir for QuadSurface::save(): " + path.string());
    }

    std::vector<cv::Mat> xyz;

    cv::split((*_points), xyz);

    cv::imwrite(path/"x.tif", xyz[0]);
    cv::imwrite(path/"y.tif", xyz[1]);
    cv::imwrite(path/"z.tif", xyz[2]);

    if (!meta)
        meta = new nlohmann::json;

    (*meta)["bbox"] = {{bbox().low[0],bbox().low[1],bbox().low[2]},{bbox().high[0],bbox().high[1],bbox().high[2]}};
    (*meta)["type"] = "seg";
    (*meta)["uuid"] = uuid;
    (*meta)["format"] = "tifxyz";
    (*meta)["scale"] = {_scale[0], _scale[1]};
    std::ofstream o(path/"meta.json.tmp");
    o << std::setw(4) << (*meta) << std::endl;

    //rename to make creation atomic
    fs::rename(path/"meta.json.tmp", path/+"meta.json");
}

void QuadSurface::save_meta()
{
    if (!meta)
        throw std::runtime_error("can't save_meta() without metadata!");
    if (path.empty())
        throw std::runtime_error("no storage path for QuadSurface");

    std::ofstream o(path/"meta.json.tmp");
    o << std::setw(4) << (*meta) << std::endl;
    
    //rename to make creation atomic
    fs::rename(path/"meta.json.tmp", path/"meta.json");
}

Rect3D QuadSurface::bbox()
{
    if (_bbox.low[0] == -1) {
        _bbox.low = (*_points)(0,0);
        _bbox.high = (*_points)(0,0);

        for(int j=0;j<_points->rows;j++)
            for(int i=0;i<_points->cols;i++)
                if (_bbox.low[0] == -1)
                    _bbox = {(*_points)(j,i),(*_points)(j,i)};
                else if ((*_points)(j,i)[0] != -1)
                    _bbox = expand_rect(_bbox, (*_points)(j,i));
    }

    return _bbox;
}

QuadSurface *load_quad_from_tifxyz(const std::string &path)
{
    std::vector<cv::Mat_<float>> xyz = {cv::imread(path+"/x.tif",cv::IMREAD_UNCHANGED),cv::imread(path+"/y.tif",cv::IMREAD_UNCHANGED),cv::imread(path+"/z.tif",cv::IMREAD_UNCHANGED)};

    auto points = new cv::Mat_<cv::Vec3f>;
    cv::merge(xyz, (*points));

    std::ifstream meta_f(path+"/meta.json");
    nlohmann::json metadata = nlohmann::json::parse(meta_f);

    cv::Vec2f scale = {metadata["scale"][0].get<float>(), metadata["scale"][1].get<float>()};

    for(int j=0;j<points->rows;j++)
        for(int i=0;i<points->cols;i++)
            //TODO fix this in the patch gen, also check bounds here in general!
            if ((*points)(j,i)[2] <= 0) {
                (*points)(j,i) = {-1,-1,-1};
            }
            
    if (fs::exists(path+"/mask.tif")) {
        std::vector<cv::Mat> layers;
        cv::imreadmulti(path+"/mask.tif", layers, cv::IMREAD_GRAYSCALE);
        cv::Mat_<uint8_t> mask = layers[0];
        cv::resize(mask, mask, points->size(), cv::INTER_NEAREST);
        for(int j=0;j<points->rows;j++)
            for(int i=0;i<points->cols;i++)
                if (!mask(j,i))
                    (*points)(j,i) = {-1,-1,-1};
    }
    
    QuadSurface *surf = new QuadSurface(points, scale);
    
    surf->path = path;
    surf->id   = metadata["uuid"];
    surf->meta = new nlohmann::json(metadata);
    
    return surf;
}

Rect3D expand_rect(const Rect3D &a, const cv::Vec3f &p)
{
    Rect3D res = a;
    for(int d=0;d<3;d++) {
        res.low[d] = std::min(res.low[d], p[d]);
        res.high[d] = std::max(res.high[d], p[d]);
    }

    return res;
}


bool intersect(const Rect3D &a, const Rect3D &b)
{
    for(int d=0;d<3;d++) {
        if (a.high[d] < b.low[d])
            return false;
        if (a.low[d] > b.high[d])
            return false;
    }

    return true;
}

Rect3D rect_from_json(const nlohmann::json &json)
{
    return {{json[0][0],json[0][1],json[0][2]},{json[1][0],json[1][1],json[1][2]}};
}

bool overlap(SurfaceMeta &a, SurfaceMeta &b, int max_iters)
{
    if (!intersect(a.bbox, b.bbox))
        return false;

    cv::Mat_<cv::Vec3f> points = a.surface()->rawPoints();
    for(int r=0;r<std::max(10, max_iters/10);r++) {
        cv::Vec2f p = {rand() % points.cols, rand() % points.rows};
        cv::Vec3f loc = points(p[1],p[0]);
        if (loc[0] == -1)
            continue;

        auto *ptr = b.surface()->pointer();

        if (b.surface()->pointTo(ptr, loc, 2.0, max_iters) <= 2.0) {
            delete ptr;
            return true;
        }
        
        delete ptr;
    }

    return false;
}

bool contains(SurfaceMeta &a, const cv::Vec3f &loc, int max_iters)
{
    if (!intersect(a.bbox, {loc,loc}))
        return false;
        
    auto *ptr = a.surface()->pointer();
    
    if (a.surface()->pointTo(ptr, loc, 2.0, max_iters) <= 2.0) {
        delete ptr;
        return true;
    }
    
    delete ptr;
    return false;
}

bool contains(SurfaceMeta &a, const std::vector<cv::Vec3f> &locs)
{
    for(auto &p : locs)
        if (!contains(a, p))
            return false;
    
    return true;
}

SurfaceMeta::SurfaceMeta(const std::filesystem::path &path_, const nlohmann::json &json) : path(path_)
{
    if (json.contains("bbox"))
        bbox = rect_from_json(json["bbox"]);
    meta = new nlohmann::json;
    *meta = json;
}

SurfaceMeta::SurfaceMeta(const std::filesystem::path &path_) : path(path_)
{
    std::ifstream meta_f(path_/"meta.json");
    if (!meta_f.is_open() || !meta_f.good()) {
        throw std::runtime_error("Cannot open meta.json file at: " + path_.string());
    }
    
    meta = new nlohmann::json;
    try {
        *meta = nlohmann::json::parse(meta_f);
    } catch (const nlohmann::json::parse_error& e) {
        delete meta;
        meta = nullptr;
        throw std::runtime_error("Invalid JSON in meta.json at: " + path_.string() + " - " + e.what());
    }
    
    if (meta->contains("bbox"))
        bbox = rect_from_json((*meta)["bbox"]);
}

SurfaceMeta::~SurfaceMeta()
{
    if (_surf) {
        delete _surf;
    }

    if (meta) {
        delete meta;
    }
}

void SurfaceMeta::readOverlapping()
{
    if (std::filesystem::exists(path / "overlapping"))
        for (const auto& entry : fs::directory_iterator(path / "overlapping"))
            overlapping_str.insert(entry.path().filename());
}

QuadSurface *SurfaceMeta::surface()
{
    if (!_surf)
        _surf = load_quad_from_tifxyz(path);
    return _surf;
}

void SurfaceMeta::setSurface(QuadSurface *surf)
{
    _surf = surf;
}

std::string SurfaceMeta::name()
{
    return path.filename();
}
