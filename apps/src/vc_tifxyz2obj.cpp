// vc_tifxyz2obj.cpp
#include "vc/core/util/Slicing.hpp"
#include "vc/core/util/Surface.hpp"
#include "vc/core/types/ChunkedTensor.hpp"

#include "z5/factory.hxx"
#include <nlohmann/json.hpp>

#include <opencv2/highgui.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <cmath>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---- small helpers ---------------------------------------------------------
static inline bool finite3(const cv::Vec3f& v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
static inline cv::Vec3f unit_or_default(const cv::Vec3f& n, const cv::Vec3f& def={0.f,0.f,1.f}) {
    float L2 = n.dot(n);
    if (!std::isfinite(L2) || L2 <= 1e-24f) return def;
    cv::Vec3f u = n / std::sqrt(L2);
    return finite3(u) ? u : def;
}
static inline cv::Vec3f fd_fallback_normal(const cv::Mat_<cv::Vec3f>& P, int y, int x) {
    // Use local finite differences (very cheap) as a robust fallback
    cv::Vec3f dx(0,0,0), dy(0,0,0);
    bool okx=false, oky=false;

    if (x+1 < P.cols && P(y,x+1)[0] != -1) { dx = P(y,x+1) - P(y,x); okx=true; }
    else if (x-1 >= 0 && P(y,x-1)[0] != -1) { dx = P(y,x) - P(y,x-1); okx=true; }

    if (y+1 < P.rows && P(y+1,x)[0] != -1) { dy = P(y+1,x) - P(y,x); oky=true; }
    else if (y-1 >= 0 && P(y-1,x)[0] != -1) { dy = P(y,x) - P(y-1,x); oky=true; }

    if (okx && oky) return unit_or_default(dx.cross(dy));
    return {0.f,0.f,1.f};
}
// ---------------------------------------------------------------------------

// Adds vertex/texcoord/normal for grid location (y,x) if not already added.
// Returns the (1-based) OBJ index for this vertex (and matching vt/vn).
static int get_add_vertex(std::ofstream& out,
                          const cv::Mat_<cv::Vec3f>& points,
                          const cv::Mat_<cv::Vec3f>& normals,
                          cv::Mat_<int>& idxs,
                          int& v_idx,
                          cv::Vec2i loc,
                          bool normalize_uv,
                          float uv_fac_x,
                          float uv_fac_y)
{

    if (idxs(loc) == -1) {
        idxs(loc) = v_idx++;
        const cv::Vec3f p = points(loc);
        out << "v " << p[0] << " " << p[1] << " " << p[2] << '\n';

        // UVs: scaled by SCALE = 20
        const float u = normalize_uv ? float(loc[1]) / float(points.cols - 1) : float(loc[1]) * uv_fac_x;
        const float v = normalize_uv ? float(loc[0]) / float(points.rows - 1) : float(loc[0]) * uv_fac_y;
        out << "vt " << u << " " << v << '\n';

        // Prefer precomputed per-vertex normal; validate; then fallback.
        cv::Vec3f n = normals(loc);
        bool ok = finite3(n);
        if (ok) n = unit_or_default(n);

        if (!ok || (n[0] == 0.f && n[1] == 0.f && n[2] == 0.f)) {
            // fall back to grid_normal (expects x,y), then to finite-diff
            cv::Vec3f ng = grid_normal(points, cv::Vec3f(float(loc[1]), float(loc[0]), 0.f));
            if (finite3(ng)) n = unit_or_default(ng);
            else             n = fd_fallback_normal(points, loc[0], loc[1]);
        }
        out << "vn " << n[0] << " " << n[1] << " " << n[2] << '\n';
    }

    return idxs(loc);
}

static cv::Mat_<cv::Vec3f> build_vertex_normals_from_faces(
        const cv::Mat_<cv::Vec3f>& P)
{
    cv::Mat_<cv::Vec3f> nsum(P.size(), cv::Vec3f(0,0,0));
    cv::Mat_<int>       ncnt(P.size(), 0);

    for (int j = 0; j < P.rows - 1; ++j) {
        for (int i = 0; i < P.cols - 1; ++i) {
            if (!loc_valid(P, cv::Vec2d(j, i))) continue;

            const cv::Vec3f p00 = P(j,   i  );
            const cv::Vec3f p01 = P(j,   i+1);
            const cv::Vec3f p10 = P(j+1, i  );
            const cv::Vec3f p11 = P(j+1, i+1);

            // Face winding matches your 'f' lines:
            // f c10 c00 c01   and   f c10 c01 c11
            const cv::Vec3f n1 = (p00 - p10).cross(p01 - p10); // (c10,c00,c01)
            const cv::Vec3f n2 = (p01 - p10).cross(p11 - p10); // (c10,c01,c11)

            auto add = [&](int y, int x, const cv::Vec3f& n) {
                nsum(y,x) += n;
                ncnt(y,x) += 1;
            };
            add(j+1,i, n1); add(j,i, n1); add(j,i+1, n1);
            add(j+1,i, n2); add(j,i+1, n2); add(j+1,i+1, n2);
        }
    }

    // normalize (and guard against degenerate cases)
    for (int y = 0; y < P.rows; ++y)
        for (int x = 0; x < P.cols; ++x) {
            cv::Vec3f n = nsum(y,x);
            float L2 = n.dot(n);
            if (ncnt(y,x) > 0 && std::isfinite(L2) && L2 > 1e-20f)
                nsum(y,x) = n / std::sqrt(L2);
            else
                nsum(y,x) = cv::Vec3f(0,0,1); // safe default (rarely used now)
        }

    return nsum;
}

static void surf_write_obj(QuadSurface *surf, const fs::path &out_fn, bool normalize_uv)
{
    cv::Mat_<cv::Vec3f> points = surf->rawPoints();
    cv::Mat_<int> idxs(points.size(), -1);

    std::ofstream out(out_fn);
    if (!out) {
        std::cerr << "Failed to open for write: " << out_fn << "\n";
        return;
    }
    out << std::fixed << std::setprecision(6);

    cv::Mat_<cv::Vec3f> normals = build_vertex_normals_from_faces(points);

    std::cout << "Point dims: " << points.size()
              << " cols: " << points.cols
              << " rows: " << points.rows << std::endl;

    // Derive UV scale from meta: surf->scale() is typically micrometers-per-pixel (or similar).
    // You asked to use the reciprocal (1/scale) as the multiplier.
    cv::Vec2f s = surf->scale();           // [sx, sy]
    float uv_fac_x = (std::isfinite(s[0]) && s[0] > 0.f) ? 1.0f / s[0] : 1.0f;
    float uv_fac_y = (std::isfinite(s[1]) && s[1] > 0.f) ? 1.0f / s[1] : 1.0f;
    if (normalize_uv) {
        std::cout << "UVs: normalized to [0,1]\n";
    } else {
        std::cout << "UVs: scaled by 1/scale from meta.json  (u*= " << uv_fac_x
                  << ", v*= " << uv_fac_y << " )\n";
        std::cout << "      (meta scale = [" << s[0] << ", " << s[1] << "])\n";
    }

    int v_idx = 1;
    for (int j = 0; j < points.rows - 1; ++j)
        for (int i = 0; i < points.cols - 1; ++i)
            if (loc_valid(points, cv::Vec2d(j, i)))
            {
                const int c00 = get_add_vertex(out, points, normals, idxs, v_idx, {j,   i  }, normalize_uv, uv_fac_x, uv_fac_y);
                const int c01 = get_add_vertex(out, points, normals, idxs, v_idx, {j,   i+1}, normalize_uv, uv_fac_x, uv_fac_y);
                const int c10 = get_add_vertex(out, points, normals, idxs, v_idx, {j+1, i  }, normalize_uv, uv_fac_x, uv_fac_y);
                const int c11 = get_add_vertex(out, points, normals, idxs, v_idx, {j+1, i+1}, normalize_uv, uv_fac_x, uv_fac_y);
                // faces unchanged: use same index for v/vt/vn
                out << "f " << c10 << "/" << c10 << "/" << c10 << " "
                           << c00 << "/" << c00 << "/" << c00 << " "
                           << c01 << "/" << c01 << "/" << c01 << '\n';

                out << "f " << c10 << "/" << c10 << "/" << c10 << " "
                           << c01 << "/" << c01 << "/" << c01 << " "
                           << c11 << "/" << c11 << "/" << c11 << '\n';
            }
}

int main(int argc, char *argv[])
{
    if (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        std::cout << "usage: " << argv[0] << " <tiffxyz> <obj> [--normalize-uv]\n";
        return EXIT_SUCCESS;
    }

    if (argc < 3 || argc > 4) {
        std::cerr << "error: wrong number of arguments\n"
                  << "usage: " << argv[0] << " <tiffxyz> <obj> [--normalize-uv]\n";
        return EXIT_FAILURE;
    }

    bool normalize_uv = false;
    if (argc == 4) {
        if (std::string(argv[3]) == "--normalize-uv")
            normalize_uv = true;
        else {
            std::cerr << "error: unknown option '" << argv[3] << "'\n";
            return EXIT_FAILURE;
        }
    }

    const fs::path seg_path = argv[1];
    const fs::path obj_path = argv[2];

    QuadSurface *surf = nullptr;
    try {
        surf = load_quad_from_tifxyz(seg_path);
    }
    catch (...) {
        std::cerr << "error when loading: " << seg_path << std::endl;
        return EXIT_FAILURE;
    }

    surf_write_obj(surf, obj_path, normalize_uv);

    delete surf;
    return EXIT_SUCCESS;
}
