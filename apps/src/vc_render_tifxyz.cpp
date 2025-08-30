#include "vc/core/util/Slicing.hpp"
#include "vc/core/util/Surface.hpp"
#include "vc/core/types/ChunkedTensor.hpp"
#include "vc/core/util/StreamOperators.hpp"

#include "z5/factory.hxx"
#include "z5/attributes.hxx"
#include <nlohmann/json.hpp>

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <boost/program_options.hpp>
#ifdef VC_HAVE_TIFF
#include <tiffio.h>
#include <mutex>
#endif

namespace fs = std::filesystem;
namespace po = boost::program_options;

using json = nlohmann::json;

/**
 * @brief Structure to hold affine transform data
 */
struct AffineTransform {
    cv::Mat_<double> matrix;  // 4x4 matrix in XYZ format
    
    AffineTransform() {
        matrix = cv::Mat_<double>::eye(4, 4);
    }
};

/**
 * @brief Load affine transform from file (JSON)
 * 
 * @param filename Path to affine transform file
 * @return AffineTransform Loaded transform data
 */
AffineTransform loadAffineTransform(const std::string& filename) {
    AffineTransform transform;
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open affine transform file: " + filename);
    }
    
    try {
        json j;
        file >> j;
        
        if (j.contains("transformation_matrix")) {
            auto mat = j["transformation_matrix"];
            if (mat.size() != 3 && mat.size() != 4) {
                throw std::runtime_error("Affine matrix must have 3 or 4 rows");
            }

            for (int row = 0; row < (int)mat.size(); row++) {
                if (mat[row].size() != 4) {
                    throw std::runtime_error("Each row of affine matrix must have 4 elements");
                }
                for (int col = 0; col < 4; col++) {
                    transform.matrix.at<double>(row, col) = mat[row][col].get<double>();
                }
            }
            // If 3x4 provided, bottom row remains [0 0 0 1] from identity ctor.
            if (mat.size() == 4) {
                // Optional: sanity-check bottom row is [0 0 0 1] within tolerance
                const double a30 = transform.matrix(3,0);
                const double a31 = transform.matrix(3,1);
                const double a32 = transform.matrix(3,2);
                const double a33 = transform.matrix(3,3);
                if (std::abs(a30) > 1e-12 || std::abs(a31) > 1e-12 ||
                    std::abs(a32) > 1e-12 || std::abs(a33 - 1.0) > 1e-12)
                    throw std::runtime_error("Bottom affine row must be [0,0,0,1]");
            }
        }
    } catch (json::parse_error&) {
        throw std::runtime_error("Error parsing affine transform file: " + filename);
    }

    return transform;
}

/**
 * @brief Print bounds and in-bounds coverage of a point field against a dataset
 */
static void debugPrintPointBounds(const cv::Mat_<cv::Vec3f>& pts,
                                  const z5::Dataset* ds,
                                  const std::string& tag)
{
    if (pts.empty()) return;
    double minx=std::numeric_limits<double>::infinity(),
           miny=std::numeric_limits<double>::infinity(),
           minz=std::numeric_limits<double>::infinity();
    double maxx=-std::numeric_limits<double>::infinity(),
           maxy=-std::numeric_limits<double>::infinity(),
           maxz=-std::numeric_limits<double>::infinity();
    size_t total=0, inb=0;
    const auto shape = ds->shape(); // [Z, Y, X]
    const double Xmax = static_cast<double>(shape[2]-1);
    const double Ymax = static_cast<double>(shape[1]-1);
    const double Zmax = static_cast<double>(shape[0]-1);
    for (int r=0; r<pts.rows; ++r) {
        for (int c=0; c<pts.cols; ++c) {
            const cv::Vec3f& p = pts(r,c);
            if (std::isnan(p[0]) || std::isnan(p[1]) || std::isnan(p[2])) continue;
            minx = std::min(minx, (double)p[0]); maxx = std::max(maxx, (double)p[0]);
            miny = std::min(miny, (double)p[1]); maxy = std::max(maxy, (double)p[1]);
            minz = std::min(minz, (double)p[2]); maxz = std::max(maxz, (double)p[2]);
            ++total;
            if (p[0] >= 0.0 && p[0] <= Xmax &&
                p[1] >= 0.0 && p[1] <= Ymax &&
                p[2] >= 0.0 && p[2] <= Zmax) ++inb;
        }
    }
    const double pct = total ? (100.0 * (double)inb / (double)total) : 0.0;
    std::cout << std::fixed << std::setprecision(2)
              << "[bounds:" << tag << "] X[" << minx << "," << maxx << "]  "
              << "Y[" << miny << "," << maxy << "]  Z[" << minz << "," << maxz << "]  "
              << "in-bounds " << pct << "% of " << total << " pts\n";
}


/**
 * @brief Apply affine transform to a single point
 * 
 * @param point Point to transform
 * @param transform Affine transform to apply
 * @return cv::Vec3f Transformed point
 */
cv::Vec3f applyAffineTransformToPoint(const cv::Vec3f& point, const AffineTransform& transform) {
    const double ptx = static_cast<double>(point[0]);
    const double pty = static_cast<double>(point[1]);
    const double ptz = static_cast<double>(point[2]);
    
    // Apply affine transform (note: matrix is in XYZ format)
    const double ptx_new = transform.matrix(0, 0) * ptx + transform.matrix(0, 1) * pty + transform.matrix(0, 2) * ptz + transform.matrix(0, 3);
    const double pty_new = transform.matrix(1, 0) * ptx + transform.matrix(1, 1) * pty + transform.matrix(1, 2) * ptz + transform.matrix(1, 3);
    const double ptz_new = transform.matrix(2, 0) * ptx + transform.matrix(2, 1) * pty + transform.matrix(2, 2) * ptz + transform.matrix(2, 3);
    
    return cv::Vec3f(
        static_cast<float>(ptx_new),
        static_cast<float>(pty_new),
        static_cast<float>(ptz_new));
}

/**
 * @brief Apply affine transform to points and normals
 * 
 * @param points Points to transform (modified in-place)
 * @param normals Normals to transform (modified in-place)
 * @param transform Affine transform to apply
 */
void applyAffineTransform(cv::Mat_<cv::Vec3f>& points, 
                         cv::Mat_<cv::Vec3f>& normals, 
                         const AffineTransform& transform) {
    // Precompute linear part A and its inverse-transpose for proper normal transform
    const cv::Matx33d A(
        transform.matrix(0,0), transform.matrix(0,1), transform.matrix(0,2),
        transform.matrix(1,0), transform.matrix(1,1), transform.matrix(1,2),
        transform.matrix(2,0), transform.matrix(2,1), transform.matrix(2,2)
    );
    // Use double precision for inversion; normals will be renormalized afterwards.
    const cv::Matx33d invAT = A.inv().t();

    // Apply transform to each point
    for (int y = 0; y < points.rows; y++) {
        for (int x = 0; x < points.cols; x++) {
            cv::Vec3f& pt = points(y, x);
            
            // Skip NaN points
            if (std::isnan(pt[0]) || std::isnan(pt[1]) || std::isnan(pt[2])) {
                continue;
            }

            pt = applyAffineTransformToPoint(pt, transform);
        }
    }
    
    // Apply correct normal transform: n' ∝ (A^{-1})^T * n (then normalize)
    for (int y = 0; y < normals.rows; y++) {
        for (int x = 0; x < normals.cols; x++) {
            cv::Vec3f& n = normals(y, x);
            if (std::isnan(n[0]) || std::isnan(n[1]) || std::isnan(n[2])) {
                continue;
            }

            const double nx_new =
                invAT(0,0) * static_cast<double>(n[0]) + invAT(0,1) * static_cast<double>(n[1]) + invAT(0,2) * static_cast<double>(n[2]);
            const double ny_new =
                invAT(1,0) * static_cast<double>(n[0]) + invAT(1,1) * static_cast<double>(n[1]) + invAT(1,2) * static_cast<double>(n[2]);
            const double nz_new =
                invAT(2,0) * static_cast<double>(n[0]) + invAT(2,1) * static_cast<double>(n[1]) + invAT(2,2) * static_cast<double>(n[2]);

            const double norm = std::sqrt(nx_new * nx_new + ny_new * ny_new + nz_new * nz_new);
            if (norm > 0.0) {
                n[0] = static_cast<float>(nx_new / norm);
                n[1] = static_cast<float>(ny_new / norm);
                n[2] = static_cast<float>(nz_new / norm);
            }
        }
    }
}


/**
 * @brief Calculate the centroid of valid 3D points in the mesh
 *
 * @param points Matrix of 3D points (cv::Mat_<cv::Vec3f>)
 * @return cv::Vec3f The centroid of all valid points
 */
cv::Vec3f calculateMeshCentroid(const cv::Mat_<cv::Vec3f>& points)
{
    cv::Vec3f centroid(0, 0, 0);
    int count = 0;

    for (int y = 0; y < points.rows; y++) {
        for (int x = 0; x < points.cols; x++) {
            const cv::Vec3f& pt = points(y, x);
            if (!std::isnan(pt[0]) && !std::isnan(pt[1]) && !std::isnan(pt[2])) {
                centroid += pt;
                count++;
            }
        }
    }

    if (count > 0) {
        centroid /= static_cast<float>(count);
    }
    return centroid;
}

/**
 * @brief Determine if normals should be flipped based on a reference point
 *
 * @param points Matrix of 3D points (cv::Mat_<cv::Vec3f>)
 * @param normals Matrix of normal vectors
 * @param referencePoint The reference point to orient normals towards/away from
 * @return bool True if normals should be flipped, false otherwise
 */
bool shouldFlipNormals(
    const cv::Mat_<cv::Vec3f>& points,
    const cv::Mat_<cv::Vec3f>& normals,
    const cv::Vec3f& referencePoint)
{
    size_t pointingToward = 0;
    size_t pointingAway = 0;

    for (int y = 0; y < points.rows; y++) {
        for (int x = 0; x < points.cols; x++) {
            const cv::Vec3f& pt = points(y, x);
            const cv::Vec3f& n = normals(y, x);

            if (std::isnan(pt[0]) || std::isnan(pt[1]) || std::isnan(pt[2]) ||
                std::isnan(n[0]) || std::isnan(n[1]) || std::isnan(n[2])) {
                continue;
            }

            // Calculate direction from point to reference
            cv::Vec3f toRef = referencePoint - pt;

            // Check if normal points toward or away from reference
            float dotProduct = toRef.dot(n);
            if (dotProduct > 0) {
                pointingToward++;
            } else {
                pointingAway++;
            }
        }
    }

    // Flip if majority point away from reference
    return pointingAway > pointingToward;
}

/**
 * @brief Apply normal flipping decision to a set of normals
 *
 * @param normals Matrix of normal vectors to potentially flip (modified in-place)
 * @param shouldFlip Whether to flip the normals
 */
void applyNormalOrientation(cv::Mat_<cv::Vec3f>& normals, bool shouldFlip)
{
    if (shouldFlip) {
        for (int y = 0; y < normals.rows; y++) {
            for (int x = 0; x < normals.cols; x++) {
                cv::Vec3f& n = normals(y, x);
                if (!std::isnan(n[0]) && !std::isnan(n[1]) && !std::isnan(n[2])) {
                    n = -n;
                }
            }
        }
    }
}

/**
 * @brief Apply rotation to an image
 *
 * @param img Image to rotate (modified in-place)
 * @param angleDegrees Rotation angle in degrees (counterclockwise)
 */
void rotateImage(cv::Mat& img, double angleDegrees)
{
    if (std::abs(angleDegrees) < 1e-6) {
        return; // No rotation needed
    }

    // Get the center of the image
    cv::Point2f center(img.cols / 2.0f, img.rows / 2.0f);

    // Get the rotation matrix
    cv::Mat rotMatrix = cv::getRotationMatrix2D(center, angleDegrees, 1.0);

    // Calculate the new image bounds
    cv::Rect2f bbox = cv::RotatedRect(cv::Point2f(), img.size(), angleDegrees).boundingRect2f();

    // Adjust transformation matrix to account for translation
    rotMatrix.at<double>(0, 2) += bbox.width / 2.0 - img.cols / 2.0;
    rotMatrix.at<double>(1, 2) += bbox.height / 2.0 - img.rows / 2.0;

    // Apply the rotation
    cv::Mat rotated;
    cv::warpAffine(img, rotated, rotMatrix, bbox.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));

    img = rotated;
}

/**
 * @brief Apply flip transformation to an image
 *
 * @param img Image to flip (modified in-place)
 * @param flipType Flip type: 0=Vertical, 1=Horizontal, 2=Both
 */
void flipImage(cv::Mat& img, int flipType)
{
    if (flipType < 0 || flipType > 2) {
        return; // Invalid flip type
    }

    if (flipType == 0) {
        // Vertical flip (flip around horizontal axis)
        cv::flip(img, img, 0);
    } else if (flipType == 1) {
        // Horizontal flip (flip around vertical axis)
        cv::flip(img, img, 1);
    } else if (flipType == 2) {
        // Both (flip around both axes)
        cv::flip(img, img, -1);
    }
}


// UV-stretch estimation for canvas sizing =====
static bool _vc_valid3(const cv::Vec3f& p) {
    return !(std::isnan(p[0]) || std::isnan(p[1]) || std::isnan(p[2]));
}

struct UVScale { double sx{1.0}, sy{1.0}; };

static UVScale estimateUVScaleFromAffine(const cv::Mat_<cv::Vec3f>& raw,
                                         const AffineTransform& T,
                                         double seg_scale)
{
    const cv::Matx33d A(
        T.matrix(0,0), T.matrix(0,1), T.matrix(0,2),
        T.matrix(1,0), T.matrix(1,1), T.matrix(1,2),
        T.matrix(2,0), T.matrix(2,1), T.matrix(2,2)
    );

    // --- remove isotropic scale so canvas reacts only to anisotropy ---
    cv::Matx33d A_used = A;
    double detA = cv::determinant(cv::Mat(A));           // det of 3x3 linear part
    double scale_factor = 1.0;
    if (std::isfinite(detA) && std::abs(detA) > 1e-18) {
        scale_factor = std::cbrt(std::abs(detA));  // isotropic scale factor (>=0)
        if (scale_factor > 0.0) {
            std::cout << "[affine] det(A)=" << detA << "  isotropic scale=" << scale_factor << "  CLI segmentation-scale=" << seg_scale << "\n";
        }
        else {
            std::cout << "[WARNING] scale factor < 0! [affine] det(A)=" << detA << "  isotropic scale=" << scale_factor << "  CLI segmentation-scale=" << seg_scale << "\n";
        }
    }
    // ----------------------------------------------------------------------

    std::vector<double> sxv, syv;
    auto push = [&](int y, int x){
        if (x+1 >= raw.cols || y+1 >= raw.rows) return;
        const cv::Vec3f &p  = raw(y,x);
        const cv::Vec3f &px = raw(y,x+1);
        const cv::Vec3f &py = raw(y+1,x);
        if (!_vc_valid3(p) || !_vc_valid3(px) || !_vc_valid3(py)) return;

        cv::Vec3d duA = (cv::Vec3d)(px - p);
        cv::Vec3d dvA = (cv::Vec3d)(py - p);

        cv::Vec3d duBg = scale_factor * seg_scale * duA;
        cv::Vec3d dvBg = scale_factor * seg_scale * dvA;

        const double duA_len = cv::norm(duA);
        const double dvA_len = cv::norm(dvA);
        if (duA_len > 1e-12) sxv.push_back(cv::norm(duBg) / duA_len);
        if (dvA_len > 1e-12) syv.push_back(cv::norm(dvBg) / dvA_len);
    };

    // Sample a few representative locations (corners + center)
    const int ys[] = {0, raw.rows/2, std::max(0, raw.rows-2)};
    const int xs[] = {0, raw.cols/2, std::max(0, raw.cols-2)};
    for (int y : ys) for (int x : xs) push(y, x);

    auto median = [](std::vector<double>& v)->double{
        if (v.empty()) return 1.0;
        size_t k = v.size()/2;
        std::nth_element(v.begin(), v.begin()+k, v.end());
        return v[k];
    };

    UVScale S;
    S.sx = median(sxv);
    S.sy = median(syv);
    return S;
}

int main(int argc, char *argv[])
{
    ///// Parse the command line options /////
    // clang-format off
    po::options_description required("Required arguments");
    required.add_options()
        ("volume,v", po::value<std::string>()->required(),
            "Path to the OME-Zarr volume")
        ("output,o", po::value<std::string>()->required(),
            "Output path or name (Zarr: name without extension; TIF: filename or printf pattern)")
        ("scale", po::value<float>()->required(),
            "Pixels per level-g voxel (Pg)")
        ("group-idx,g", po::value<int>()->required(),
            "OME-Zarr group index");

    po::options_description optional("Optional arguments");
    optional.add_options()
        ("help,h", "Show this help message")
        ("segmentation,s", po::value<std::string>(),
            "Path to a single tifxyz segmentation folder (ignored if --render-folder is set)")
        ("render-folder", po::value<std::string>(),
            "Folder containing tifxyz segmentation folders to batch render")
        ("format", po::value<std::string>(),
            "When using --render-folder, choose 'zarr' or 'tif' output")
        ("num-slices,n", po::value<int>()->default_value(1),
            "Number of slices to render")
        ("crop-x", po::value<int>()->default_value(0),
            "Crop region X coordinate")
        ("crop-y", po::value<int>()->default_value(0),
            "Crop region Y coordinate")
        ("crop-width", po::value<int>()->default_value(0),
            "Crop region width (0 = no crop)")
        ("crop-height", po::value<int>()->default_value(0),
            "Crop region height (0 = no crop)")
        ("affine-transform", po::value<std::string>(),
            "Path to affine transform file (JSON; key 'transformation_matrix' 3x4 or 4x4)")
        ("invert-affine", po::bool_switch()->default_value(false),
            "Invert the given affine before applying (useful if JSON is voxel->world)")
        ("scale-segmentation", po::value<float>()->default_value(1.0),
            "Scale segmentation to target scale")
        ("rotate", po::value<double>()->default_value(0.0),
            "Rotate output image by angle in degrees (counterclockwise)")
        ("flip", po::value<int>()->default_value(-1),
            "Flip output image. 0=Vertical, 1=Horizontal, 2=Both")
        ("include-tifs", po::bool_switch()->default_value(false),
            "If output is Zarr, also export per-Z TIFF slices to layers_{zarrname}");
    // clang-format on

    po::options_description all("Usage");
    all.add(required).add(optional);

    // Parse command line
    po::variables_map parsed;
    try {
        po::store(po::command_line_parser(argc, argv).options(all).run(), parsed);
        
        // Show help message
        if (parsed.count("help") > 0 || argc < 2) {
            std::cout << "vc_render_tifxyz: Render volume data using segmentation surfaces\n\n";
            std::cout << all << '\n';
            return EXIT_SUCCESS;
        }
        
        po::notify(parsed);
    } catch (po::error& e) {
        std::cerr << "Error: " << e.what() << '\n';
        std::cerr << "Use --help for usage information\n";
        return EXIT_FAILURE;
    }

    // Extract parsed arguments
    fs::path vol_path = parsed["volume"].as<std::string>();
    std::string base_output_arg = parsed["output"].as<std::string>();
    const bool has_render_folder = parsed.count("render-folder") > 0;
    fs::path render_folder_path;
    std::string batch_format;
    if (has_render_folder) {
        render_folder_path = fs::path(parsed["render-folder"].as<std::string>());
        if (parsed.count("format") == 0) {
            std::cerr << "Error: --format is required when using --render-folder (zarr|tif).\n";
            return EXIT_FAILURE;
        }
        batch_format = parsed["format"].as<std::string>();
        std::transform(batch_format.begin(), batch_format.end(), batch_format.begin(), ::tolower);
        if (batch_format != "zarr" && batch_format != "tif") {
            std::cerr << "Error: --format must be 'zarr' or 'tif'.\n";
            return EXIT_FAILURE;
        }
        if (!fs::exists(render_folder_path) || !fs::is_directory(render_folder_path)) {
            std::cerr << "Error: --render-folder path is not a directory: " << render_folder_path << "\n";
            return EXIT_FAILURE;
        }
    }
    fs::path seg_path;
    if (!has_render_folder) {
        if (parsed.count("segmentation") == 0) {
            std::cerr << "Error: --segmentation is required unless --render-folder is used.\n";
            return EXIT_FAILURE;
        }
        seg_path = parsed["segmentation"].as<std::string>();
    }
    float tgt_scale = parsed["scale"].as<float>();
    int group_idx = parsed["group-idx"].as<int>();
    int num_slices = parsed["num-slices"].as<int>();
    // Downsample factor for this OME-Zarr pyramid level: g=0 -> 1, g=1 -> 0.5, ...
    const float ds_scale = std::ldexp(1.0f, -group_idx);  // 2^(-group_idx)
    float scale_seg = parsed["scale-segmentation"].as<float>();
    // Transformation parameters
    double rotate_angle = parsed["rotate"].as<double>();
    const bool invert_affine = parsed["invert-affine"].as<bool>();
    int flip_axis = parsed["flip"].as<int>();
    const bool include_tifs = parsed["include-tifs"].as<bool>();

    // Load affine transform if provided
    AffineTransform affineTransform;
    bool hasAffine = false;
    
    if (parsed.count("affine-transform") > 0) {
        std::string affineFile = parsed["affine-transform"].as<std::string>();
        try {
            affineTransform = loadAffineTransform(affineFile);
            hasAffine = true;
            std::cout << "Loaded affine transform from: " << affineFile << std::endl;
            if (invert_affine) {
                // Invert full 4x4 (double precision)
                cv::Mat inv = cv::Mat(affineTransform.matrix).inv();
                if (inv.empty()) {
                    std::cerr << "Error: affine matrix is non-invertible.\n";
                    return EXIT_FAILURE;
                }
                inv.copyTo(affineTransform.matrix);
                std::cout << "Note: Inverting affine as requested (--invert-affine).\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "Error loading affine transform: " << e.what() << std::endl;
            return EXIT_FAILURE;
        }
    }
    
    z5::filesystem::handle::Group group(vol_path, z5::FileMode::FileMode::r);
    z5::filesystem::handle::Dataset ds_handle(group, std::to_string(group_idx), json::parse(std::ifstream(vol_path/std::to_string(group_idx)/".zarray")).value<std::string>("dimension_separator","."));
    std::unique_ptr<z5::Dataset> ds = z5::filesystem::openDataset(ds_handle);

    std::cout << "zarr dataset size for scale group " << group_idx << ds->shape() << std::endl;
    std::cout << "chunk shape shape " << ds->chunking().blockShape() << std::endl;
    std::cout << "output argument: " << base_output_arg << std::endl;

    if (std::abs(rotate_angle) > 1e-6) {
        std::cout << "Rotation: " << rotate_angle << " degrees" << std::endl;
    }
    if (flip_axis >= 0) {
        std::cout << "Flip: " << (flip_axis == 0 ? "Vertical" : flip_axis == 1 ? "Horizontal" : "Both") << std::endl;
    }

    // Prepare dataset handle (volume) shared across renders
    
    ChunkCache chunk_cache(16ull * 1024 * 1024 * 1024);

    auto process_one = [&](const fs::path& seg_folder, const std::string& out_arg, bool force_zarr){
        fs::path output_path_local(out_arg);
        if (force_zarr) {
            // ensure .zarr extension
            if (output_path_local.extension() != ".zarr")
                output_path_local = output_path_local.string() + ".zarr";
        }
        bool output_is_zarr = force_zarr || (output_path_local.extension() == ".zarr");
        if (!output_is_zarr) {
            // May be a directory target (no printf pattern): create directory
            if (output_path_local.string().find('%') == std::string::npos) {
                fs::create_directories(output_path_local);
            } else {
                fs::create_directories(output_path_local.parent_path());
            }
        }

        std::cout << "Rendering segmentation: "
                  << seg_folder.string() << " -> "
                  << output_path_local.string()
                  << (output_is_zarr?" (zarr)":" (tif)")
                  << std::endl;

        QuadSurface *surf = nullptr;
        try {
            surf = load_quad_from_tifxyz(seg_folder);
        }
        catch (...) {
            std::cout << "error when loading: " << seg_folder << std::endl;
            return;
        }

    cv::Mat_<cv::Vec3f> *raw_points = surf->rawPointsPtr();
    for(int j=0;j<raw_points->rows;j++)
        for(int i=0;i<raw_points->cols;i++)
            if ((*raw_points)(j,i)[0] == -1)
                (*raw_points)(j,i) = {NAN,NAN,NAN};
    
    cv::Size full_size = raw_points->size();

    // Interpret --scale as Pg = pixels per level-g voxel.
    // Compute isotropic affine scale sA = cbrt(|det(A)|) (ignore shear/rot)
    // and the effective render scale used by surf->gen() and canvas sizing:
    //   render_scale = Pg / (scale_seg * sA * ds_scale)
    // This keeps pixels locked to level-g voxels while geometry is still
    // mapped to dataset index space by: scale_seg -> affine -> ds_scale.
    double sA = 1.0;
    if (hasAffine) {
        const cv::Matx33d A(
            affineTransform.matrix(0,0), affineTransform.matrix(0,1), affineTransform.matrix(0,2),
            affineTransform.matrix(1,0), affineTransform.matrix(1,1), affineTransform.matrix(1,2),
            affineTransform.matrix(2,0), affineTransform.matrix(2,1), affineTransform.matrix(2,2)
        );
        const double detA = cv::determinant(cv::Mat(A));
        if (std::isfinite(detA) && std::abs(detA) > 1e-18)
            sA = std::cbrt(std::abs(detA));
    }
    const double Pg = static_cast<double>(tgt_scale);
    const double render_scale = Pg * (static_cast<double>(scale_seg) * sA * static_cast<double>(ds_scale));

    // Canvas sizing depends ONLY on render_scale and the saved surface stride.
    {
        const double sx = render_scale / static_cast<double>(surf->_scale[0]);
        const double sy = render_scale / static_cast<double>(surf->_scale[1]);
        full_size.width  = std::max(1, static_cast<int>(std::lround(full_size.width  * sx)));
        full_size.height = std::max(1, static_cast<int>(std::lround(full_size.height * sy)));
    }
    
    cv::Size tgt_size = full_size;
    cv::Rect crop = {0,0,tgt_size.width, tgt_size.height};
    
    std::cout << "downsample level " << group_idx
              << " (ds_scale=" << ds_scale << ", sA=" << sA
              << ", Pg=" << Pg << ", render_scale=" << render_scale << ")\n";

    // Handle crop parameters
    int crop_x = parsed["crop-x"].as<int>();
    int crop_y = parsed["crop-y"].as<int>();
    int crop_width = parsed["crop-width"].as<int>();
    int crop_height = parsed["crop-height"].as<int>();
    
    if (crop_width > 0 && crop_height > 0) {
        crop = {crop_x, crop_y, crop_width, crop_height};
        tgt_size = crop.size();
    }        
    
    std::cout << "rendering size " << tgt_size << " at scale " << tgt_scale << " crop " << crop << std::endl;
    
    cv::Mat_<cv::Vec3f> points, normals;
    
    bool slice_gen = false;
    
    // Global normal orientation decision (for consistency across chunks)
    bool globalFlipDecision = false;
    bool orientationDetermined = false;
    cv::Vec3f meshCentroid;

    if ((tgt_size.width >= 10000 || tgt_size.height >= 10000) && num_slices > 1)
        slice_gen = true;
    else {
        // Center at pixel centers: -(W-1)/2, -(H-1)/2
        const float u0 = -0.5f * (static_cast<float>(tgt_size.width)  - 1.0f);
        const float v0 = -0.5f * (static_cast<float>(tgt_size.height) - 1.0f);
        surf->gen(&points, &normals,
                  tgt_size, cv::Vec3f(0,0,0),
                  static_cast<float>(render_scale),
                  cv::Vec3f(u0, v0, 0.0f));
    }

    cv::Mat_<uint8_t> img;

    // If output is a Zarr, render directly into a multi-resolution OME-Zarr pyramid
    if (output_is_zarr) {
        // Use the same render_scale as TIF path to ensure identical XY mapping
        const double render_scale_zarr = render_scale;

        // Prepare geometry sizing (no rotation/flip for Zarr writing)
        // and final canvas size (tgt_size)
        cv::Mat_<cv::Vec3f> points, normals;

        // Center at pixel centers for the full canvas
        const float u0_full = -0.5f * (static_cast<float>(tgt_size.width)  - 1.0f);
        const float v0_full = -0.5f * (static_cast<float>(tgt_size.height) - 1.0f);

        // We'll generate tiles on-demand; determine orientation from the first tile we generate.

        // Create OME-Zarr with 6 levels (0..5)
        // Chunking: X/Y = 128; Z = number of rendered slices (baseZ)
        const size_t CH = 128, CW = 128;
        const size_t baseZ = std::max(1, num_slices);
        const size_t CZ = baseZ;
        cv::Size zarr_xy_size = tgt_size;
        if (std::abs(rotate_angle) > 1e-6) {
            // Compute rotated bounding box dimensions without materializing an image
            cv::RotatedRect rr(cv::Point2f(), cv::Size2f((float)zarr_xy_size.width, (float)zarr_xy_size.height), rotate_angle);
            cv::Rect2f bbox = rr.boundingRect2f();
            zarr_xy_size.width  = std::max(1, (int)std::lround(bbox.width));
            zarr_xy_size.height = std::max(1, (int)std::lround(bbox.height));
        }
        const size_t baseY = static_cast<size_t>(zarr_xy_size.height);
        const size_t baseX = static_cast<size_t>(zarr_xy_size.width);

        z5::filesystem::handle::File outFile(output_path_local);
        z5::createFile(outFile, true);

        auto make_shape = [](size_t z, size_t y, size_t x){
            return std::vector<size_t>{z, y, x};
        };

        auto make_chunks = [](size_t z, size_t y, size_t x){
            return std::vector<size_t>{z, y, x};
        };

        // Create base dataset level 0
        std::vector<size_t> shape0 = make_shape(baseZ, baseY, baseX);
        std::vector<size_t> chunks0 = make_chunks(shape0[0], std::min(CH, shape0[1]), std::min(CW, shape0[2]));
        // Compressor: blosc + zstd (no shuffle)
        nlohmann::json compOpts0 = {
            {"cname",   "zstd"},
            {"clevel",  1},
            {"shuffle", 0}
        };
        auto dsOut0 = z5::createDataset(outFile, "0", "uint8", shape0, chunks0, std::string("blosc"), compOpts0);

        // Progress tracking across XY tiles
        const size_t tilesY = (shape0[1] + CH - 1) / CH;
        const size_t tilesX = (shape0[2] + CW - 1) / CW;
        const size_t totalTiles = tilesY * tilesX;
        std::atomic<size_t> tilesDone{0};

        // Decide orientation once using a small sample tile (after applying transforms)
        bool globalFlipDecision = false;
        {
            const size_t dx0 = std::min(CW, shape0[2]);
            const size_t dy0 = std::min(CH, shape0[1]);
            const float u0 = u0_full;
            const float v0 = v0_full;
            cv::Mat_<cv::Vec3f> _tp, _tn;
            surf->gen(&_tp, &_tn,
                      cv::Size(static_cast<int>(dx0), static_cast<int>(dy0)),
                      cv::Vec3f(0,0,0),
                      static_cast<float>(render_scale_zarr),
                      cv::Vec3f(u0, v0, 0.0f));
            // Apply transforms for orientation decision
            _tp *= scale_seg;
            if (hasAffine) {
                applyAffineTransform(_tp, _tn, affineTransform);
            }
            meshCentroid = calculateMeshCentroid(_tp);
            globalFlipDecision = shouldFlipNormals(_tp, _tn, meshCentroid);
        }

        // Iterate output chunks and render directly into them (parallel over XY tiles)
        for (size_t z0 = 0; z0 < shape0[0]; z0 += CZ) {
            const size_t dz = std::min(CZ, shape0[0] - z0);
            #pragma omp parallel for schedule(dynamic) collapse(2)
            for (long long y0 = 0; y0 < static_cast<long long>(shape0[1]); y0 += CH) {
                for (long long x0 = 0; x0 < static_cast<long long>(shape0[2]); x0 += CW) {
                    const size_t dy = std::min(CH, static_cast<size_t>(shape0[1] - y0));
                    const size_t dx = std::min(CW, static_cast<size_t>(shape0[2] - x0));

                    // Generate base coordinates and normals for this tile once (dx x dy)
                    const float u0 = u0_full + static_cast<float>(x0);
                    const float v0 = v0_full + static_cast<float>(y0);

                    cv::Mat_<cv::Vec3f> tilePoints, tileNormals;
                    surf->gen(&tilePoints, &tileNormals,
                              cv::Size(static_cast<int>(dx), static_cast<int>(dy)),
                              cv::Vec3f(0,0,0),
                              static_cast<float>(render_scale_zarr),
                              cv::Vec3f(u0, v0, 0.0f));
                    // Transform points/normals to dataset space: scale_seg -> affine -> ds_scale
                    cv::Mat_<cv::Vec3f> basePoints = tilePoints.clone();
                    basePoints *= scale_seg;
                    cv::Mat_<cv::Vec3f> nrm = tileNormals.clone();
                    if (hasAffine) {
                        applyAffineTransform(basePoints, nrm, affineTransform);
                    }
                    // Orientation
                    applyNormalOrientation(nrm, globalFlipDecision);
                    // Normalize normals to get step directions
                    cv::Mat_<cv::Vec3f> stepDirs = nrm.clone();
                    for (int yy = 0; yy < stepDirs.rows; ++yy)
                        for (int xx = 0; xx < stepDirs.cols; ++xx) {
                            cv::Vec3f &v = stepDirs(yy,xx);
                            if (std::isnan(v[0]) || std::isnan(v[1]) || std::isnan(v[2])) continue;
                            float L = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
                            if (L > 0) v /= L;
                        }
                    basePoints *= ds_scale;

                    // Allocate output chunk buffer [Z,Y,X]
                    xt::xarray<uint8_t> outChunk = xt::empty<uint8_t>({dz, dy, dx});

                    // Fill per-slice using cached interpolator
                    // Reuse buffers across slices
                    cv::Mat_<cv::Vec3f> coords(tilePoints.size());
                    cv::Mat_<uint8_t> tileOut;
                    for (size_t zi = 0; zi < dz; ++zi) {
                        const size_t sliceIndex = z0 + zi;
                        const float off = static_cast<float>(static_cast<double>(sliceIndex) - 0.5 * (static_cast<double>(baseZ) - 1.0));
                        // Compute coords in-place to avoid temporary allocations (mesh already in dataset coords)
                        for (int yy = 0; yy < coords.rows; ++yy) {
                            for (int xx = 0; xx < coords.cols; ++xx) {
                                const cv::Vec3f& p = basePoints(yy, xx);
                                const cv::Vec3f& d = stepDirs(yy, xx);
                                coords(yy, xx) = p + off * d * static_cast<float>(ds_scale);
                            }
                        }
                        readInterpolated3D(tileOut, ds.get(), coords, &chunk_cache);

                        // Copy to outChunk
                        for (size_t yy = 0; yy < dy; ++yy) {
                            for (size_t xx = 0; xx < dx; ++xx) {
                                outChunk(zi, yy, xx) = tileOut(static_cast<int>(yy), static_cast<int>(xx));
                            }
                        }
                    }

                    // Write chunk to Zarr
                    z5::types::ShapeType outOffset = {z0, static_cast<size_t>(y0), static_cast<size_t>(x0)};
                    z5::multiarray::writeSubarray<uint8_t>(dsOut0, outChunk, outOffset.begin());

                    size_t done = ++tilesDone;
                    int pct = static_cast<int>(100.0 * double(done) / double(totalTiles));
                    #pragma omp critical(progress_print)
                    {
                        std::cout << "\r[render L0] tile " << done << "/" << totalTiles
                                  << " (" << pct << "%)" << std::flush;
                    }
                }
            }
        }

        // After finishing L0 tiles, add newline for the progress line
        std::cout << std::endl;

        // Build multi-resolution pyramid levels 1..5 by averaging 2x blocks in Z, Y, and X
        auto downsample_avg = [&](int targetLevel){
            auto src = z5::openDataset(outFile, std::to_string(targetLevel - 1));
            const auto& sShape = src->shape();
            // Downsample Z, Y and X by 2 (handle odd sizes)
            std::vector<size_t> dShape = {
                (sShape[0] + 1) / 2,
                (sShape[1] + 1) / 2,
                (sShape[2] + 1) / 2
            };
            // Chunk Z equals number of slices at this level (full Z), XY = 128
            std::vector<size_t> dChunks = make_chunks(dShape[0], std::min(CH, dShape[1]), std::min(CW, dShape[2]));
            nlohmann::json compOpts = {
                {"cname",   "zstd"},
                {"clevel",  1},
                {"shuffle", 0}
            };
            auto dst = z5::createDataset(outFile, std::to_string(targetLevel), "uint8", dShape, dChunks, std::string("blosc"), compOpts);

            // Iterate in tiles respecting chunking: write whole Z at once
            const size_t tileZ = dShape[0], tileY = CH, tileX = CW;
            // Progress for this level
            const size_t tilesY = (dShape[1] + tileY - 1) / tileY;
            const size_t tilesX = (dShape[2] + tileX - 1) / tileX;
            const size_t totalTiles = tilesY * tilesX;
            std::atomic<size_t> tilesDone{0};

            for (size_t z = 0; z < dShape[0]; z += tileZ) {
                const size_t lz = std::min(tileZ, dShape[0] - z);
                #pragma omp parallel for schedule(dynamic) collapse(2)
                for (long long y = 0; y < static_cast<long long>(dShape[1]); y += tileY) {
                    for (long long x = 0; x < static_cast<long long>(dShape[2]); x += tileX) {
                        const size_t ly = std::min(tileY, static_cast<size_t>(dShape[1] - y));
                        const size_t lx = std::min(tileX, static_cast<size_t>(dShape[2] - x));

                        // Read source subarray for this output tile. 2x in Z and XY
                        const size_t sz = std::min<size_t>(2*lz, sShape[0] - 2*z);
                        const size_t sy = std::min<size_t>(2*ly, sShape[1] - y*2);
                        const size_t sx = std::min<size_t>(2*lx, sShape[2] - x*2);

                        xt::xarray<uint8_t> srcChunk = xt::empty<uint8_t>({sz, sy, sx});
                        {
                            z5::types::ShapeType sOff = {static_cast<size_t>(2*z), static_cast<size_t>(2*y), static_cast<size_t>(2*x)};
                            z5::multiarray::readSubarray<uint8_t>(src, srcChunk, sOff.begin());
                        }

                        xt::xarray<uint8_t> dstChunk = xt::empty<uint8_t>({lz, ly, lx});
                        for (size_t zz = 0; zz < lz; ++zz) {
                            for (size_t yy = 0; yy < ly; ++yy) {
                                for (size_t xx = 0; xx < lx; ++xx) {
                                    // Average available neighbors in 2x2x2 window (handle odd sizes at edges)
                                    int sum = 0;
                                    int cnt = 0;
                                    for (int dz2 = 0; dz2 < 2 && (2*zz + dz2) < sz; ++dz2)
                                        for (int dy2 = 0; dy2 < 2 && (2*yy + dy2) < sy; ++dy2)
                                            for (int dx2 = 0; dx2 < 2 && (2*xx + dx2) < sx; ++dx2) {
                                                sum += srcChunk(2*zz + dz2, 2*yy + dy2, 2*xx + dx2);
                                                cnt += 1;
                                            }
                                    dstChunk(zz, yy, xx) = static_cast<uint8_t>((sum + (cnt/2)) / std::max(1, cnt));
                                }
                            }
                        }

                        z5::types::ShapeType dOff = {z, static_cast<size_t>(y), static_cast<size_t>(x)};
                        z5::multiarray::writeSubarray<uint8_t>(dst, dstChunk, dOff.begin());

                        size_t done = ++tilesDone;
                        int pct = static_cast<int>(100.0 * double(done) / double(totalTiles));
                        #pragma omp critical(progress_print)
                        {
                            std::cout << "\r[render L" << targetLevel << "] tile " << done << "/" << totalTiles
                                      << " (" << pct << "%)" << std::flush;
                        }
                    }
                }
            }
            // newline after finishing this level
            std::cout << std::endl;
        };

        for (int level = 1; level <= 5; ++level) {
            downsample_avg(level);
        }

        // Root attributes including OME-NGFF multiscales
        nlohmann::json attrs;
        attrs["source_zarr"] = vol_path.string();
        attrs["source_group"] = group_idx;
        attrs["num_slices"] = baseZ;
        {
            cv::Size attr_xy = tgt_size;
            if (std::abs(rotate_angle) > 1e-6) {
                cv::RotatedRect rr(cv::Point2f(), cv::Size2f((float)attr_xy.width, (float)attr_xy.height), rotate_angle);
                cv::Rect2f bbox = rr.boundingRect2f();
                attr_xy.width  = std::max(1, (int)std::lround(bbox.width));
                attr_xy.height = std::max(1, (int)std::lround(bbox.height));
            }
            attrs["canvas_size"] = {attr_xy.width, attr_xy.height};
        }
        attrs["chunk_size"] = {static_cast<int>(CZ), static_cast<int>(CH), static_cast<int>(CW)};
        attrs["note_axes_order"] = "ZYX (slice, row, col)";

        // OME-NGFF multiscales v0.4 metadata
        nlohmann::json multiscale;
        multiscale["version"] = "0.4";
        multiscale["name"] = "render";
        multiscale["axes"] = nlohmann::json::array({
            nlohmann::json{{"name","z"},{"type","space"},{"unit","pixel"}},
            nlohmann::json{{"name","y"},{"type","space"},{"unit","pixel"}},
            nlohmann::json{{"name","x"},{"type","space"},{"unit","pixel"}}
        });
        multiscale["datasets"] = nlohmann::json::array();
        for (int level = 0; level <= 5; ++level) {
            const double s = std::pow(2.0, level);
            nlohmann::json dset;
            dset["path"] = std::to_string(level);
            dset["coordinateTransformations"] = nlohmann::json::array({
                // Z, Y and X scale by 2^level
                nlohmann::json{{"type","scale"},{"scale", nlohmann::json::array({s, s, s})}},
                nlohmann::json{{"type","translation"},{"translation", nlohmann::json::array({0.0, 0.0, 0.0})}}
            });
            multiscale["datasets"].push_back(dset);
        }
        multiscale["metadata"] = nlohmann::json{{"downsampling_method","mean"}};
        attrs["multiscales"] = nlohmann::json::array({multiscale});

        z5::writeAttributes(outFile, attrs);

        // Optionally export per-Z TIFFs from level 0 into layers_{zarrname}
        if (include_tifs) {
            try {
                auto dsL0 = z5::openDataset(outFile, "0");
                const auto& shape0_check = dsL0->shape(); // [Z, Y, X]
                const size_t Z = shape0_check[0];
                const int Y = static_cast<int>(shape0_check[1]);
                const int X = static_cast<int>(shape0_check[2]);

                // Build output folder next to the .zarr directory
                std::string zname = output_path_local.stem().string();
                fs::path layers_dir = output_path_local.parent_path() / (std::string("layers_") + zname);
                fs::create_directories(layers_dir);

                // Zero padding width: at least 2 digits
                int pad = 2;
                size_t maxIndex = (Z > 0) ? (Z - 1) : 0;
                while (maxIndex >= 100) { pad++; maxIndex /= 10; }

                // If all expected TIFFs already exist, skip export
                bool all_exist = true;
                for (size_t z = 0; z < Z; ++z) {
                    std::ostringstream fn;
                    fn << std::setw(pad) << std::setfill('0') << z;
                    fs::path outPath = layers_dir / (fn.str() + std::string(".tif"));
                    if (!fs::exists(outPath)) { all_exist = false; break; }
                }
                if (all_exist) {
                    std::cout << "[tif export] all slices exist in " << layers_dir.string() << ", skipping." << std::endl;
                    // Nothing else to do
                    delete surf;
                    return;
                }

#ifdef VC_HAVE_TIFF
                // Fast path: write tiled TIFFs directly per slice using libtiff.
                const uint32_t tileW = static_cast<uint32_t>(CW);
                const uint32_t tileH = static_cast<uint32_t>(CH);
                const uint32_t tilesX = (static_cast<uint32_t>(X) + tileW - 1) / tileW;
                const uint32_t tilesY = (static_cast<uint32_t>(Y) + tileH - 1) / tileH;
                const size_t totalTiles = static_cast<size_t>(tilesX) * static_cast<size_t>(tilesY);
                std::atomic<size_t> tilesDone{0};

                std::vector<TIFF*> tiffs(Z, nullptr);
                std::vector<std::mutex> tiffLocks(Z); // per-slice locks
                for (size_t z = 0; z < Z; ++z) {
                    std::ostringstream fn;
                    fn << std::setw(pad) << std::setfill('0') << z;
                    fs::path outPath = layers_dir / (fn.str() + std::string(".tif"));
                    TIFF* tf = TIFFOpen(outPath.string().c_str(), "w8");
                    if (!tf) throw std::runtime_error("failed to open TIFF for writing: " + outPath.string());
                    TIFFSetField(tf, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(X));
                    TIFFSetField(tf, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(Y));
                    TIFFSetField(tf, TIFFTAG_SAMPLESPERPIXEL, 1);
                    TIFFSetField(tf, TIFFTAG_BITSPERSAMPLE, 8);
                    TIFFSetField(tf, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
                    TIFFSetField(tf, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
                    TIFFSetField(tf, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
                    TIFFSetField(tf, TIFFTAG_PREDICTOR, 2);
                    TIFFSetField(tf, TIFFTAG_TILEWIDTH, tileW);
                    TIFFSetField(tf, TIFFTAG_TILELENGTH, tileH);
                    tiffs[z] = tf;
                }

                // Per-thread tile buffer padded to tile size
                #pragma omp parallel for schedule(dynamic) collapse(2)
                for (long long ty = 0; ty < static_cast<long long>(tilesY); ++ty) {
                    for (long long tx = 0; tx < static_cast<long long>(tilesX); ++tx) {
                        const uint32_t x0 = static_cast<uint32_t>(tx) * tileW;
                        const uint32_t y0 = static_cast<uint32_t>(ty) * tileH;
                        const uint32_t dx = std::min<uint32_t>(tileW, static_cast<uint32_t>(X) - x0);
                        const uint32_t dy = std::min<uint32_t>(tileH, static_cast<uint32_t>(Y) - y0);

                        xt::xarray<uint8_t> tile = xt::empty<uint8_t>({Z, static_cast<size_t>(dy), static_cast<size_t>(dx)});
                        {
                            z5::types::ShapeType off = {0, static_cast<size_t>(y0), static_cast<size_t>(x0)};
                            z5::multiarray::readSubarray<uint8_t>(dsL0, tile, off.begin());
                        }

                        std::vector<uint8_t> tileBuf(tileW * tileH, 0);
                        for (size_t z = 0; z < Z; ++z) {
                            // Fill pad buffer
                            for (uint32_t yy = 0; yy < dy; ++yy) {
                                uint8_t* dst = tileBuf.data() + yy * tileW;
                                for (uint32_t xx = 0; xx < dx; ++xx) {
                                    dst[xx] = tile(z, yy, xx);
                                }
                                // zero padding already present beyond dx
                            }
                            // Write tile to the corresponding TIFF (per-slice lock)
                            {
                                std::lock_guard<std::mutex> guard(tiffLocks[z]);
                                TIFF* tf = tiffs[z];
                                const uint32_t tileIndex = TIFFComputeTile(tf, x0, y0, 0, 0);
                                if (TIFFWriteEncodedTile(tf, tileIndex, tileBuf.data(), static_cast<tmsize_t>(tileBuf.size())) < 0) {
                                    // TODO: optional error handling/logging per tile
                                }
                            }
                        }

                        // Progress update per completed tile (all slices written for this tile)
                        size_t done = ++tilesDone;
                        int pct = static_cast<int>(100.0 * double(done) / double(totalTiles));
                        #pragma omp critical(progress_print)
                        {
                            std::cout << "\r[tif export] tiles " << done << "/" << totalTiles
                                      << " (" << pct << "%)" << std::flush;
                        }
                    }
                }

                for (auto* tf : tiffs) {
                    TIFFClose(tf);
                }

                std::cout << std::endl;
#else
                // Fallback: assemble full images in memory (OpenCV) and write
                // Allocate one full-size image per slice in memory
                std::vector<cv::Mat> images;
                images.reserve(Z);
                for (size_t z = 0; z < Z; ++z) images.emplace_back(Y, X, CV_8UC1);

                const size_t tilesY = (static_cast<size_t>(Y) + CH - 1) / CH;
                const size_t tilesX = (static_cast<size_t>(X) + CW - 1) / CW;
                #pragma omp parallel for schedule(dynamic) collapse(2)
                for (long long ty = 0; ty < static_cast<long long>(tilesY); ++ty) {
                    for (long long tx = 0; tx < static_cast<long long>(tilesX); ++tx) {
                        const size_t y0 = static_cast<size_t>(ty) * CH;
                        const size_t x0 = static_cast<size_t>(tx) * CW;
                        const size_t dy = std::min(static_cast<size_t>(CH), static_cast<size_t>(Y) - y0);
                        const size_t dx = std::min(static_cast<size_t>(CW), static_cast<size_t>(X) - x0);
                        xt::xarray<uint8_t> tile = xt::empty<uint8_t>({Z, dy, dx});
                        z5::types::ShapeType off = {0, y0, x0};
                        z5::multiarray::readSubarray<uint8_t>(dsL0, tile, off.begin());
                        for (size_t z = 0; z < Z; ++z) {
                            cv::Mat roi = images[z](cv::Rect(static_cast<int>(x0), static_cast<int>(y0), static_cast<int>(dx), static_cast<int>(dy)));
                            for (size_t yy = 0; yy < dy; ++yy) std::memcpy(roi.ptr<uint8_t>(static_cast<int>(yy)), &tile(z, yy, 0), dx);
                        }
                    }
                }
                std::atomic<size_t> done{0};
                #pragma omp parallel for schedule(dynamic)
                for (long long zi = 0; zi < static_cast<long long>(Z); ++zi) {
                    std::ostringstream fn; fn << std::setw(pad) << std::setfill('0') << zi;
                    fs::path outPath = layers_dir / (fn.str() + std::string(".tif"));
                    cv::imwrite(outPath.string(), images[static_cast<size_t>(zi)]);
                    size_t d = ++done;
                    #pragma omp critical(progress_print)
                    { std::cout << "\r[tif export] slice " << d << "/" << Z << std::flush; }
                }
                std::cout << std::endl;
#endif
            } catch (const std::exception& e) {
                std::cerr << "[tif export] warning: failed to export TIFFs: " << e.what() << std::endl;
            }
        }

        delete surf;
        return;
    }

    if (num_slices == 1) {
        // Scale the segmentation points if requested
        points *= scale_seg;

        // Apply affine transform if provided
        if (hasAffine) {
            std::cout << "Applying affine transform to points and normals for single slice" << std::endl;
            applyAffineTransform(points, normals, affineTransform);
        }

        // Apply downsample scaling AFTER affine so translation is scaled too
        points *= ds_scale;

        // Decide global orientation after full transform (once)
        if (!orientationDetermined) {
            meshCentroid = calculateMeshCentroid(points);
            globalFlipDecision = shouldFlipNormals(points, normals, meshCentroid);
            orientationDetermined = true;
            std::cout << "Orienting normals to point consistently ("
                      << (globalFlipDecision ? "flipped" : "not flipped") << ")" << std::endl;
        }
        applyNormalOrientation(normals, globalFlipDecision);

        readInterpolated3D(img, ds.get(), points, &chunk_cache);

        // Debug: where did we sample?
        debugPrintPointBounds(points, ds.get(), "single-slice/post-affine+ds");

        // Apply transformations
        if (std::abs(rotate_angle) > 1e-6) {
            rotateImage(img, rotate_angle);
        }
        if (flip_axis >= 0) {
            flipImage(img, flip_axis);
        }

        if (!output_is_zarr) {
            if (output_path_local.string().find('%') == std::string::npos) {
                // Default numeric name in directory: 00.tif
                fs::path out_file = output_path_local / "00.tif";
                cv::imwrite(out_file.string(), img);
            } else {
                cv::imwrite(output_path_local.string(), img);
            }
        } else {
            // Not possible here because output_is_zarr branch above handles zarr
            cv::imwrite(output_path_local.string(), img);
        }
    }
    else {
        char buf[1024];
        for(int i=0;i<num_slices;i++) {
            float off = i - 0.5f * (num_slices - 1);
            if (slice_gen) {
                img.create(tgt_size);

                // For chunked processing, we need to determine orientation from the first chunk
                // or a representative sample to ensure consistency
                for(int x=crop.x;x<crop.x+crop.width;x+=1024) {
                    int w = std::min(tgt_size.width+crop.x-x, 1024);
                    // Independent-crop FOV: local chunk origin inside the crop
                    const float u0 = -0.5f * (static_cast<float>(tgt_size.width)  - 1.0f)
                                   + static_cast<float>(x - crop.x);
                    const float v0 = -0.5f * (static_cast<float>(tgt_size.height) - 1.0f);
                    surf->gen(&points, &normals,
                              cv::Size(w, crop.height),
                              cv::Vec3f(0,0,0),
                              static_cast<float>(render_scale),
                              cv::Vec3f(u0, v0, 0.0f));
                    // Scale the segmentation points if requested
                    points *= scale_seg;

                    // Apply affine transform if provided
                    if (hasAffine) {
                        std::cout << "Applying affine transform to points and normals for slice " << i << std::endl;
                        applyAffineTransform(points, normals, affineTransform);
                    }
                    // Build forward step vectors: use the already-correct transformed normals
                    // (n' ∝ inv(A)^T * n, normalized inside applyAffineTransform).
                    cv::Mat_<cv::Vec3f> stepDirs = normals.clone();
                    // Ensure unit length even when hasAffine == false.
                    for (int yy = 0; yy < stepDirs.rows; ++yy)
                        for (int xx = 0; xx < stepDirs.cols; ++xx) {
                            cv::Vec3f &v = stepDirs(yy,xx);
                            if (std::isnan(v[0]) || std::isnan(v[1]) || std::isnan(v[2])) continue;
                            float L = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
                            if (L > 0) v /= L;
                        }
                    // Determine orientation from first chunk if not yet determined
                    if (!orientationDetermined) {
                        meshCentroid = calculateMeshCentroid(points);
                        globalFlipDecision = shouldFlipNormals(points, normals, meshCentroid);
                        orientationDetermined = true;

                        if (globalFlipDecision) {
                            std::cout << "Orienting normals to point consistently (flipped) - determined from first chunk" << std::endl;
                        } else {
                            std::cout << "Orienting normals to point consistently (not flipped) - determined from first chunk" << std::endl;
                        }
                    }

                    // Apply the consistent orientation decision to all chunks
                    applyNormalOrientation(normals, globalFlipDecision);
                    applyNormalOrientation(stepDirs, globalFlipDecision);

                    cv::Mat_<uint8_t> slice;
                    readInterpolated3D(slice, ds.get(),
                        points*ds_scale + off*stepDirs*ds_scale, &chunk_cache);
                    debugPrintPointBounds(points*ds_scale + off*stepDirs*ds_scale,
                                          ds.get(), "chunk/post-affine+ds");
                    slice.copyTo(img(cv::Rect(x-crop.x,0,w,crop.height)));
                }
            }
            else {
                // Build base coordinates in dataset space: scale_seg -> affine -> ds_scale
                cv::Mat_<cv::Vec3f> basePoints = points.clone();

                // Scale segmentation points
                basePoints *= scale_seg;

                // Apply affine to points and normals
                if (hasAffine) {
                    std::cout << "Applying affine transform to points and normals for slice " << i << " for non-slice_gen case" << std::endl;
                    cv::Mat_<cv::Vec3f> tmpNormals = normals.clone();
                    applyAffineTransform(basePoints, tmpNormals, affineTransform);
                    // Decide/apply consistent normal orientation once
                    if (!orientationDetermined) {
                        meshCentroid = calculateMeshCentroid(basePoints);
                        globalFlipDecision = shouldFlipNormals(basePoints, tmpNormals, meshCentroid);
                        orientationDetermined = true;
                        std::cout << "Orienting normals to point consistently ("
                                  << (globalFlipDecision ? "flipped" : "not flipped")
                                  << ") - determined from first slice" << std::endl;
                    }
                    applyNormalOrientation(tmpNormals, globalFlipDecision);
                    // Compute forward step directions from the corrected normals
                    cv::Mat_<cv::Vec3f> stepDirs = tmpNormals.clone();
                    // Ensure unit length and orientation are consistent
                    for (int yy = 0; yy < stepDirs.rows; ++yy)
                        for (int xx = 0; xx < stepDirs.cols; ++xx) {
                            cv::Vec3f &v = stepDirs(yy,xx);
                            if (std::isnan(v[0]) || std::isnan(v[1]) || std::isnan(v[2])) continue;
                            float L = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
                            if (L > 0) v /= L;
                        }
                    applyNormalOrientation(stepDirs, globalFlipDecision);
                    // Apply downsample scaling AFTER affine so translation is scaled too
                    basePoints *= ds_scale;
                    cv::Mat_<cv::Vec3f> offsetPoints = basePoints + off * stepDirs * ds_scale;
                    readInterpolated3D(img, ds.get(), offsetPoints, &chunk_cache);
                    debugPrintPointBounds(offsetPoints, ds.get(),
                                          "noslice/post-affine+ds");
                } else {
                    // No affine: decide/apply consistent normal orientation once here if needed
                    if (!orientationDetermined) {
                        meshCentroid = calculateMeshCentroid(basePoints);
                        globalFlipDecision = shouldFlipNormals(basePoints, normals, meshCentroid);
                        orientationDetermined = true;
                        std::cout << "Orienting normals to point consistently ("
                                  << (globalFlipDecision ? "flipped" : "not flipped")
                                  << ") - determined without affine" << std::endl;
                    }
                    // Forward step = raw normals (normalize + orient)
                    cv::Mat_<cv::Vec3f> stepDirs = normals.clone();
                    for (int yy = 0; yy < stepDirs.rows; ++yy)
                        for (int xx = 0; xx < stepDirs.cols; ++xx) {
                            cv::Vec3f &v = stepDirs(yy,xx);
                            if (std::isnan(v[0]) || std::isnan(v[1]) || std::isnan(v[2])) continue;
                            float L = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
                            if (L > 0) v /= L;
                        }
                    applyNormalOrientation(stepDirs, globalFlipDecision);
                    // Apply downsample scaling AFTER (no affine)
                    basePoints *= ds_scale;
                    cv::Mat_<cv::Vec3f> offsetPoints = basePoints + off * stepDirs * ds_scale;
                    readInterpolated3D(img, ds.get(), offsetPoints, &chunk_cache);
                }
            }
            
            // Apply transformations
            if (std::abs(rotate_angle) > 1e-6) {
                rotateImage(img, rotate_angle);
            }
            if (flip_axis >= 0) {
                flipImage(img, flip_axis);
            }
            if (!output_is_zarr && output_path_local.string().find('%') == std::string::npos) {
                // Write into directory with default zero-padded names (min width 2)
                int pad = 2;
                int v = i;
                while (v >= 100) { pad++; v /= 10; }
                std::ostringstream fn;
                fn << std::setw(pad) << std::setfill('0') << i;
                fs::path out_file = output_path_local / (fn.str() + ".tif");
                cv::imwrite(out_file.string(), img);
            } else {
                snprintf(buf, 1024, output_path_local.string().c_str(), i);
                cv::imwrite(buf, img);
            }
        }
    }

    delete surf;
    };

    if (has_render_folder) {
        // Iterate subfolders and render those that look like tifxyz (contain x.tif, y.tif, z.tif)
        size_t count = 0;
        for (auto const& entry : fs::directory_iterator(render_folder_path)) {
            if (!entry.is_directory()) continue;
            fs::path f = entry.path();
            if (!(fs::exists(f/"x.tif") && fs::exists(f/"y.tif") && fs::exists(f/"z.tif"))) continue;
            // Compute output argument relative to this tifxyz folder
            std::string out_arg;
            bool force_zarr = false;
            if (batch_format == "zarr") {
                out_arg = (f / base_output_arg).string();
                // zarr output path with enforced extension
                fs::path zarr_path = out_arg;
                if (zarr_path.extension() != ".zarr") zarr_path = zarr_path.string() + ".zarr";
                if (fs::exists(zarr_path)) {
                    std::cout << "[skip] exists: " << zarr_path << "\n";
                    continue;
                }
                force_zarr = true;
            } else { // tif
                // If no printf pattern present, interpret -o as a directory name under the seg folder
                // Default filenames will be 00.tif ...
                out_arg = (f / base_output_arg).string();
                if (out_arg.find('%') == std::string::npos) {
                    fs::path out_dir(out_arg);
                    // Determine last expected filename with width >= 2
                    int pad = 2;
                    size_t last = (num_slices > 0) ? (num_slices - 1) : 0;
                    size_t t = last;
                    while (t >= 100) { pad++; t /= 10; }
                    std::ostringstream oss;
                    oss << std::setw(pad) << std::setfill('0') << last;
                    fs::path last_file = out_dir / (oss.str() + ".tif");
                    if (fs::exists(last_file)) {
                        std::cout << "[skip] exists: " << last_file << "\n";
                        continue;
                    }
                } else {
                    // Pattern present: test existence of the last file in the pattern
                    char testbuf[1024];
                    int pad = 2; // try to honor at least 2 digits even if pattern smaller
                    size_t last = (num_slices > 0) ? (num_slices - 1) : 0;
                    // Render into buffer
                    snprintf(testbuf, sizeof(testbuf), out_arg.c_str(), (int)last);
                    if (fs::exists(testbuf)) {
                        std::cout << "[skip] exists: " << testbuf << "\n";
                        continue;
                    }
                }
            }
            process_one(f, out_arg, force_zarr);
            ++count;
        }
        if (count == 0) {
            std::cout << "No tifxyz folders found in: " << render_folder_path << std::endl;
        }
        return EXIT_SUCCESS;
    } else {
        // Single segmentation path
        process_one(seg_path, base_output_arg, false);
        return EXIT_SUCCESS;
    }
}
