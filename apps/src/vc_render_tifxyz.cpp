#include "vc/core/util/Slicing.hpp"
#include "vc/core/util/Surface.hpp"
#include "vc/core/types/ChunkedTensor.hpp"
#include "vc/core/util/StreamOperators.hpp"

#include "z5/factory.hxx"
#include <nlohmann/json.hpp>

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <fstream>
#include <boost/program_options.hpp>

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




int main(int argc, char *argv[])
{
    ///// Parse the command line options /////
    // clang-format off
    po::options_description required("Required arguments");
    required.add_options()
        ("volume,v", po::value<std::string>()->required(),
            "Path to the OME-Zarr volume")
        ("output,o", po::value<std::string>()->required(),
            "Output path/pattern for rendered images")
        ("segmentation,s", po::value<std::string>()->required(),
            "Path to the segmentation file")
        ("scale", po::value<float>()->required(),
            "Target scale for rendering")
        ("group-idx,g", po::value<int>()->required(),
            "OME-Zarr group index");

    po::options_description optional("Optional arguments");
    optional.add_options()
        ("help,h", "Show this help message")
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
            "Flip output image. 0=Vertical, 1=Horizontal, 2=Both");
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
    std::string tgt_ptn = parsed["output"].as<std::string>();
    fs::path seg_path = parsed["segmentation"].as<std::string>();
    float tgt_scale = parsed["scale"].as<float>();
    int group_idx = parsed["group-idx"].as<int>();
    int num_slices = parsed["num-slices"].as<int>();
    // Downsample factor for this OME-Zarr pyramid level: g=0 -> 1, g=1 -> 0.5, ...
    const float ds_scale = std::ldexp(1.0f, -group_idx);  // 2^(-group_idx)
    float scale_seg = parsed["scale-segmentation"].as<float>();
    // Effective render scale for UV sampling:
    // If the seg mesh is in volume A (downscaled /2 vox) and we later scale coordinates by 'scale_seg'
    // (to get to A full res), we must counterbalance here so pixel density stays constant.
    const float inv_scale_seg_sq = 1.0f / (scale_seg * scale_seg);
    const float tgt_scale_eff = (tgt_scale * ds_scale) * inv_scale_seg_sq;
    // Transformation parameters
    double rotate_angle = parsed["rotate"].as<double>();
    const bool invert_affine = parsed["invert-affine"].as<bool>();
    int flip_axis = parsed["flip"].as<int>();
    
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
    std::cout << "saving output to " << tgt_ptn << std::endl;

    if (std::abs(rotate_angle) > 1e-6) {
        std::cout << "Rotation: " << rotate_angle << " degrees" << std::endl;
    }
    if (flip_axis >= 0) {
        std::cout << "Flip: " << (flip_axis == 0 ? "Vertical" : flip_axis == 1 ? "Horizontal" : "Both") << std::endl;
    }

    fs::path output_path(tgt_ptn);
    fs::create_directories(output_path.parent_path());
    
    ChunkCache chunk_cache(16e9);

    QuadSurface *surf = nullptr;
    try {
        surf = load_quad_from_tifxyz(seg_path);
    }
    catch (...) {
        std::cout << "error when loading: " << seg_path << std::endl;
        return EXIT_FAILURE;
    }

    cv::Mat_<cv::Vec3f> *raw_points = surf->rawPointsPtr();
    for(int j=0;j<raw_points->rows;j++)
        for(int i=0;i<raw_points->cols;i++)
            if ((*raw_points)(j,i)[0] == -1)
                (*raw_points)(j,i) = {NAN,NAN,NAN};
    
    cv::Size full_size = raw_points->size();
    // Auto-scale the canvas by the pyramid level, so -g N shrinks by 2^N.
    // Use rounding to avoid truncation bias.
    {
        const double sx = (static_cast<double>(tgt_scale) /  surf->_scale[0]) * ds_scale * scale_seg;
        const double sy = (static_cast<double>(tgt_scale) /  surf->_scale[1]) * ds_scale * scale_seg;
        full_size.width  = std::max(1, static_cast<int>(std::lround(full_size.width  * sx)));
        full_size.height = std::max(1, static_cast<int>(std::lround(full_size.height * sy)));
    }
    
    cv::Size tgt_size = full_size;
    cv::Rect crop = {0,0,tgt_size.width, tgt_size.height};
    
    std::cout << "downsample level " << group_idx << " (ds_scale=" << ds_scale
              << "), effective render scale " << tgt_scale_eff << std::endl;

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
        // Use effective scale so UV sampling spans the same world area even though
        // canvas is different by scale_seg (FOV stays constant).
        surf->gen(&points, &normals, tgt_size, cv::Vec3f(0,0,0), tgt_scale_eff, {-full_size.width/2+crop.x,-full_size.height/2+crop.y,0});
    }

    cv::Mat_<uint8_t> img;

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

        cv::imwrite(tgt_ptn.c_str(), img);
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
                    // Apply effective scale in chunked generation
                    surf->gen(&points, &normals, {w,crop.height}, cv::Vec3f(0,0,0), tgt_scale_eff, {-full_size.width/2+x,-full_size.height/2+crop.y,0});

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

                    const float stepScale = ds_scale * scale_seg;
                    cv::Mat_<uint8_t> slice;
                    readInterpolated3D(slice, ds.get(),
                        points*ds_scale + off*stepDirs*stepScale, &chunk_cache);
                    debugPrintPointBounds(points*ds_scale + off*stepDirs*stepScale,
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
                    // Add slice offset in dataset units
                    const float stepScale = ds_scale * scale_seg;
                    cv::Mat_<cv::Vec3f> offsetPoints = basePoints + off * stepDirs * stepScale;
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
                    const float stepScale = ds_scale * scale_seg;
                    cv::Mat_<cv::Vec3f> offsetPoints = basePoints + off * stepDirs * stepScale;
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
            snprintf(buf, 1024, tgt_ptn.c_str(), i);
            cv::imwrite(buf, img);
        }
    }

    delete surf;

    return EXIT_SUCCESS;
}
