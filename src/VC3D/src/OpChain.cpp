#include "OpChain.hpp"

#include "vc/Surface.hpp"
#include "vc/Slicing.hpp"

void OpChain::append(DeltaSurface *op)
{
    _ops.push_back(op);
}

SurfacePointer *OpChain::pointer()
{
    return _src->pointer();
}

void OpChain::move(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    _src->move(ptr, offset);
}

bool OpChain::valid(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    return _src->valid(ptr, offset);
}

cv::Vec3f OpChain::loc(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    return _src->loc(ptr, offset);
}

cv::Vec3f OpChain::coord(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    //FIXME use cached surface? Or regen
    return _src->coord(ptr, offset);
}

cv::Vec3f OpChain::normal(SurfacePointer *ptr, const cv::Vec3f &offset)
{
    //FIXME use cached surface? Or regen
    return _src->normal(ptr, offset);
}

float OpChain::pointTo(SurfacePointer *ptr, const cv::Vec3f &coord, float th, int max_iters)
{
    //FIXME use cached surf? Or use src surface?
    return _src->pointTo(ptr, coord, th, max_iters);
}

void OpChain::gen(cv::Mat_<cv::Vec3f> *coords, cv::Mat_<cv::Vec3f> *normals, cv::Size size, SurfacePointer *ptr, float scale, const cv::Vec3f &offset)
{
    Surface *last = nullptr;
    SurfacePointer *ptr_center = ptr;
    if (!ptr_center)
        ptr_center = _src->pointer();

    if (_crop) {
        delete _crop;
        _crop = nullptr;
    }

    if (_src_mode == OpChainSourceMode::RAW) {
        last = _src;
    }
    else if (_src_mode == OpChainSourceMode::BLUR) {
        if (!_src_blur)
            _src_blur = smooth_vc_segmentation(_src);
        
        last = _src_blur;
    }

    //reset op chain
    for(auto s : _ops) {
        if (!enabled(s))
            continue;
        s->setBase(last);
        last = s;
    }

    if (_src_mode == OpChainSourceMode::RAW || _src_mode == OpChainSourceMode::BLUR) {
        last->gen(coords, normals, size, ptr, scale, offset);
    }
    else
        last->gen(coords, normals, size, nullptr, scale, {-size.width/2, -size.height/2, ((TrivialSurfacePointer*)ptr_center)->loc[2]+offset[2]});
}

const char *op_name(Surface *op)
{
    if (!op)
        return "";

    if (dynamic_cast<OpChain*>(op))
        return "source";
    if (dynamic_cast<RefineCompSurface*>(op))
        return "refineAlphaComp";
    return "FIXME unknown op name";
}


void OpChain::setEnabled(DeltaSurface *surf, bool enabled)
{
    if (enabled)
        _disabled.erase(surf);
    else
        _disabled.insert(surf);
}

bool OpChain::enabled(DeltaSurface *surf)
{
    return _disabled.count(surf) == 0;
}
