#include "vc/core/util/Slicing.hpp"
#include "vc/core/util/Surface.hpp"
#include "vc/core/types/ChunkedTensor.hpp"

#include "z5/factory.hxx"
#include <nlohmann/json.hpp>

#include <opencv2/highgui.hpp>

#include <iomanip>

namespace fs = std::filesystem;

using json = nlohmann::json;

int get_add_vertex(std::ofstream &out, cv::Mat_<cv::Vec3f> &points,
                   cv::Mat_<int> idxs, int &v_idx, cv::Vec2i loc,
                   bool normalize_uv)
{
    if (idxs(loc) == -1) {
        idxs(loc) = v_idx++;
        cv::Vec3f p = points(loc);
        out << "v " << p[0] << " " << p[1] << " " << p[2] << std::endl;

        // Normalize if flag
        float u = normalize_uv ? (1.0f * loc[1] / points.cols)
                               : static_cast<float>(loc[1]);
        float v = normalize_uv ? (1.0f * loc[0] / points.rows)
                               : static_cast<float>(loc[0]);
        out << "vt " << u << " " << v << std::endl;

        cv::Vec3f n = grid_normal(points, {loc[1],loc[0]});

        if (n[0] == n[0] && n[1] == n[1] && n[2] == n[2])
            out << "vn " << n[0] << " " << n[1] << " " << n[2] << std::endl;
        else
            out << "vn 0 0 0" << std::endl;
    }

    return idxs(loc);
}

void surf_write_obj(QuadSurface *surf, const fs::path &out_fn,
                    bool normalize_uv)
{
    cv::Mat_<cv::Vec3f> points = surf->rawPoints();
    cv::Mat_<int> idxs(points.size(), -1);

    cv::Mat_<cv::Vec3f> coords, normals;

    std::ofstream out(out_fn);

    out << std::fixed << std::setprecision(6);

    std::cout << "Point dims: " << points.size() << " cols: " << points.cols << " rows: " << points.rows << std::endl;

    int v_idx = 1;
    for(int j=0;j<points.rows-1;j++)
        for(int i=0;i<points.cols-1;i++)
            if (loc_valid(points, {j,i}))
            {
                int c00 = get_add_vertex(out, points, idxs, v_idx, {j,i}, normalize_uv);
                int c01 = get_add_vertex(out, points, idxs, v_idx, {j,i+1}, normalize_uv);
                int c10 = get_add_vertex(out, points, idxs, v_idx, {j+1,i}, normalize_uv);
                int c11 = get_add_vertex(out, points, idxs, v_idx, {j+1,i+1}, normalize_uv);

                out << "f " << c10 << "/" << c10 << "/" << c10 << " " << c00 << "/" << c00 << "/" << c00 << " " << c01 << "/" << c01 << "/" << c01 << std::endl;
                out << "f " << c10 << "/" << c10 << "/" << c10 << " " << c01 << "/" << c01 << "/" << c01 << " " << c11 << "/" << c11 << "/" << c11 << std::endl;
            }
}

int main(int argc, char *argv[])
{
    if (argc < 3 || argc > 4) {
        std::cout << "usage: " << argv[0]
                  << " <tiffxyz> <obj> [--normalize-uv]" << std::endl;
        return EXIT_SUCCESS;
    }

    fs::path seg_path = argv[1];
    fs::path obj_path = argv[2];

    // Default behaviour: (un-normalized UVs)
    bool normalize_uv = false;
    if (argc == 4 && std::string(argv[3]) == "--normalize-uv")
        normalize_uv = true;

    QuadSurface *surf = nullptr;
    try {
        surf = load_quad_from_tifxyz(seg_path);
    }
    catch (...) {
        std::cout << "error when loading: " << seg_path << std::endl;
        return EXIT_FAILURE;
    }

    surf_write_obj(surf, obj_path, normalize_uv);

    delete surf;

    return EXIT_SUCCESS;
}
