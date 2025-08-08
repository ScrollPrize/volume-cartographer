#include <nlohmann/json.hpp>

#include "vc/core/util/xtensor_include.hpp"
#include XTENSORINCLUDE(containers, xarray.hpp)
#include XTENSORINCLUDE(views, xaxis_slice_iterator.hpp)
#include XTENSORINCLUDE(io, xio.hpp)
#include XTENSORINCLUDE(generators, xbuilder.hpp)
#include XTENSORINCLUDE(views, xview.hpp)

#include "z5/factory.hxx"
#include "z5/filesystem/handle.hxx"
#include "z5/filesystem/dataset.hxx"
#include "z5/common.hxx"
#include "z5/multiarray/xtensor_access.hxx"
#include "z5/attributes.hxx"

#include <opencv2/highgui.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "vc/core/util/Slicing.hpp"
#include "vc/core/util/Surface.hpp"
#include "vc/core/io/PointSetIO.hpp"

#include <unordered_map>
#include <filesystem>
#include <omp.h>

#include "../../core/src/SurfaceHelpers.hpp"
#include "vc/core/types/ChunkedTensor.hpp"

using shape = z5::types::ShapeType;
using namespace xt::placeholders;
namespace fs = std::filesystem;

using json = nlohmann::json;

std::ostream& operator<< (std::ostream& out, const xt::svector<size_t> &v) {
    if ( !v.empty() ) {
        out << '[';
        for(auto &v : v)
            out << v << ",";
        out << "\b]"; // use ANSI backspace character '\b' to overwrite final ", "
    }
    return out;
}

class MeasureLife
{
public:
    MeasureLife(std::string msg)
    {
        std::cout << msg << std::flush;
        start = std::chrono::high_resolution_clock::now();
    }
    ~MeasureLife()
    {
        auto end = std::chrono::high_resolution_clock::now();
        std::cout << " took " << std::chrono::duration<double>(end-start).count() << " s" << std::endl;
    }
private:
    std::chrono::time_point<std::chrono::high_resolution_clock> start;
};



template <typename T, typename I>
float get_val(I &interp, cv::Vec3d l) {
    T v;
    interp.Evaluate(l[2], l[1], l[0], &v);
    return v;
}

int main(int argc, char *argv[])
{
    if (argc != 6) {
        std::cout << "usage: " << argv[0] << " <zarr-volume> <src-dir> <tgt-dir> <json-params> <src-segment>" << std::endl;
        return EXIT_SUCCESS;
    }

    fs::path vol_path = argv[1];
    fs::path src_dir = argv[2];
    fs::path tgt_dir = argv[3];
    fs::path params_path = argv[4];
    fs::path src_path = argv[5];
    while (src_path.filename().empty())
        src_path = src_path.parent_path();

    std::ifstream params_f(params_path);
    json params = json::parse(params_f);
    params["tgt_dir"] = tgt_dir;

    z5::filesystem::handle::Group group(vol_path, z5::FileMode::FileMode::r);
    z5::filesystem::handle::Dataset ds_handle(group, "0", json::parse(std::ifstream(vol_path/"0/.zarray")).value<std::string>("dimension_separator","."));
    std::unique_ptr<z5::Dataset> ds = z5::filesystem::openDataset(ds_handle);

    std::cout << "zarr dataset size for scale group 0 " << ds->shape() << std::endl;
    std::cout << "chunk shape shape " << ds->chunking().blockShape() << std::endl;

    float voxelsize = json::parse(std::ifstream(vol_path/"meta.json"))["voxelsize"];

    std::string name_prefix = "auto_grown_";
    std::vector<SurfaceMeta*> surfaces;

    fs::path meta_fn = src_path / "meta.json";
    std::ifstream meta_f(meta_fn);
    json meta = json::parse(meta_f);
    SurfaceMeta *src = new SurfaceMeta(src_path, meta);
    src->readOverlapping();

    for (const auto& entry : fs::directory_iterator(src_dir))
        if (fs::is_directory(entry)) {
            std::string name = entry.path().filename();
            if (name.compare(0, name_prefix.size(), name_prefix))
                continue;

            fs::path meta_fn = entry.path() / "meta.json";
            if (!fs::exists(meta_fn))
                continue;

            std::ifstream meta_f(meta_fn);
            json meta = json::parse(meta_f);

            if (!meta.count("bbox"))
                continue;

            if (meta.value("format","NONE") != "tifxyz")
                continue;

            SurfaceMeta *sm;
            if (entry.path().filename() == src->name())
                sm = src;
            else {
                sm = new SurfaceMeta(entry.path(), meta);
                sm->readOverlapping();
            }

            surfaces.push_back(sm);
        }

    QuadSurface *surf = grow_surf_from_surfs(src, surfaces, params, voxelsize);

    if (!surf)
        return EXIT_SUCCESS;

    (*surf->meta)["source"] = "vc_grow_seg_from_segments";
    (*surf->meta)["vc_grow_seg_from_segments_params"] = params;
    std::string uuid = "auto_trace_" + get_surface_time_str();;
    fs::path seg_dir = tgt_dir / uuid;
    surf->save(seg_dir, uuid);

    delete surf;
    for(auto sm : surfaces) {
        delete sm;
    }

    return EXIT_SUCCESS;
}
