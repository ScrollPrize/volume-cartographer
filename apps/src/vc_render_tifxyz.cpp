#include "vc/core/util/Slicing.hpp"
#include "vc/core/util/Surface.hpp"
#include "vc/core/types/ChunkedTensor.hpp"

#include "z5/factory.hxx"
#include <nlohmann/json.hpp>

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <fstream>
#include <sstream>
#include <boost/program_options.hpp>

namespace fs = std::filesystem;
namespace po = boost::program_options;

using json = nlohmann::json;

/**
 * @brief Structure to hold affine transform data
 */
struct AffineTransform {
    cv::Mat_<float> matrix;  // 4x4 matrix in XYZ format
    
    AffineTransform() {
        matrix = cv::Mat_<float>::eye(4, 4);
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
            if (mat.size() != 3) {
                throw std::runtime_error("Affine matrix must have 3 rows");
            }
            
            for (int row = 0; row < 3; row++) {
                if (mat[row].size() != 4) {
                    throw std::runtime_error("Each row of affine matrix must have 4 elements");
                }
                for (int col = 0; col < 4; col++) {
                    transform.matrix.at<float>(row, col) = mat[row][col].get<float>();
                }
            }
        }
    } catch (json::parse_error&) {
        throw std::runtime_error("Error parsing affine transform file: " + filename);
    }

    return transform;
}

/**
 * @brief Apply affine transform to a single point
 * 
 * @param point Point to transform
 * @param transform Affine transform to apply
 * @return cv::Vec3f Transformed point
 */
cv::Vec3f applyAffineTransformToPoint(const cv::Vec3f& point, const AffineTransform& transform) {
    float ptx = point[0];
    float pty = point[1];
    float ptz = point[2];
    
    // Apply affine transform (note: matrix is in XYZ format)
    float ptx_new = transform.matrix(0, 0) * ptx + transform.matrix(0, 1) * pty + transform.matrix(0, 2) * ptz + transform.matrix(0, 3);
    float pty_new = transform.matrix(1, 0) * ptx + transform.matrix(1, 1) * pty + transform.matrix(1, 2) * ptz + transform.matrix(1, 3);
    float ptz_new = transform.matrix(2, 0) * ptx + transform.matrix(2, 1) * pty + transform.matrix(2, 2) * ptz + transform.matrix(2, 3);
    
    return cv::Vec3f(ptx_new, pty_new, ptz_new);
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
    
    // Apply transform to normals (rotation only, no translation)
    for (int y = 0; y < normals.rows; y++) {
        for (int x = 0; x < normals.cols; x++) {
            cv::Vec3f& n = normals(y, x);
            
            // Skip NaN normals
            if (std::isnan(n[0]) || std::isnan(n[1]) || std::isnan(n[2])) {
                continue;
            }
            
            float nx = n[0];
            float ny = n[1];
            float nz = n[2];
            
            // Apply rotation part of affine transform
            float nx_new = transform.matrix(0, 0) * nx + transform.matrix(0, 1) * ny + transform.matrix(0, 2) * nz;
            float ny_new = transform.matrix(1, 0) * nx + transform.matrix(1, 1) * ny + transform.matrix(1, 2) * nz;
            float nz_new = transform.matrix(2, 0) * nx + transform.matrix(2, 1) * ny + transform.matrix(2, 2) * nz;
            
            // Normalize the transformed normal
            float norm = std::sqrt(nx_new * nx_new + ny_new * ny_new + nz_new * nz_new);
            if (norm > 0) {
                n[0] = nx_new / norm;
                n[1] = ny_new / norm;
                n[2] = nz_new / norm;
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

std::ostream& operator<< (std::ostream& out, const xt::svector<size_t> &v) {
    if ( !v.empty() ) {
        out << '[';
        for(auto &v : v)
            out << v << ",";
        out << "\b]";
    }
    return out;
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
            "Path to affine transform file (JSON or text format)")
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
    float scale_seg = parsed["scale-segmentation"].as<float>();
    // Transformation parameters
    double rotate_angle = parsed["rotate"].as<double>();
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
    full_size.width *= tgt_scale/surf->_scale[0];
    full_size.height *= tgt_scale/surf->_scale[1];
    
    cv::Size tgt_size = full_size;
    cv::Rect crop = {0,0,tgt_size.width, tgt_size.height};
    
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

    if (tgt_size.width >= 10000 && num_slices > 1)
        slice_gen = true;
    else {
        surf->gen(&points, &normals, tgt_size, cv::Vec3f(0,0,0), tgt_scale, {-full_size.width/2+crop.x,-full_size.height/2+crop.y,0});

        // Calculate the actual mesh centroid
        meshCentroid = calculateMeshCentroid(points);

        globalFlipDecision = shouldFlipNormals(points, normals, meshCentroid);
        orientationDetermined = true;
        if (globalFlipDecision) {
            std::cout << "Orienting normals to point consistently (flipped)" << std::endl;
        } else {
            std::cout << "Orienting normals to point consistently (not flipped)" << std::endl;
        }
        applyNormalOrientation(normals, globalFlipDecision);
    }

    cv::Mat_<uint8_t> img;

    float ds_scale = pow(2,-group_idx);
    if (group_idx && !slice_gen) {
        points *= ds_scale;
    }

    if (num_slices == 1) {
        // Scale the segmentation points if requested
        points *= scale_seg;

        // Apply affine transform if provided
        if (hasAffine) {
            std::cout << "Applying affine transform to points and normals for single slice" << std::endl;
            applyAffineTransform(points, normals, affineTransform);
        }

        readInterpolated3D(img, ds.get(), points, &chunk_cache);

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
            float off = i-num_slices/2;
            if (slice_gen) {
                img.create(tgt_size);

                // For chunked processing, we need to determine orientation from the first chunk
                // or a representative sample to ensure consistency
                for(int x=crop.x;x<crop.x+crop.width;x+=1024) {
                    int w = std::min(tgt_size.width+crop.x-x, 1024);
                    surf->gen(&points, &normals, {w,crop.height}, cv::Vec3f(0,0,0), tgt_scale, {-full_size.width/2+x,-full_size.height/2+crop.y,0});
                    
                    // Scale the segmentation points if requested
                    points *= scale_seg;

                    // Apply affine transform if provided
                    if (hasAffine) {
                        std::cout << "Applying affine transform to points and normals for slice " << i << std::endl;
                        applyAffineTransform(points, normals, affineTransform);
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

                    cv::Mat_<uint8_t> slice;
                    readInterpolated3D(slice, ds.get(), points*ds_scale+off*normals*ds_scale, &chunk_cache);
                    slice.copyTo(img(cv::Rect(x-crop.x,0,w,crop.height)));
                }
            }
            else {
                cv::Mat_<cv::Vec3f> offsetPoints = points + off*ds_scale*normals;
                // Scale the segmentation points if requested
                offsetPoints *= scale_seg;

                // Apply affine transform if provided (for non-slice_gen case)
                if (hasAffine) {
                    std::cout << "Applying affine transform to points and normals for slice " << i << " for non-slice_gen case" << std::endl;
                    cv::Mat_<cv::Vec3f> offsetNormals = normals.clone();
                    applyAffineTransform(offsetPoints, offsetNormals, affineTransform);
                }
                readInterpolated3D(img, ds.get(), offsetPoints, &chunk_cache);
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
