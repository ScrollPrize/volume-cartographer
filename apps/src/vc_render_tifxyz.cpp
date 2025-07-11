#include "vc/core/util/Slicing.hpp"
#include "vc/core/util/Surface.hpp"
#include "vc/core/types/ChunkedTensor.hpp"

#include "z5/factory.hxx"
#include <nlohmann/json.hpp>

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

namespace fs = std::filesystem;

using json = nlohmann::json;

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

void printUsage(const char* programName)
{
    std::cout << "Usage: " << programName << " <ome-zarr-volume> <output> <seg-path> <tgt-scale> <ome-zarr-group-idx> [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  <num-slices>                    Number of slices to render (default: 1)" << std::endl;
    std::cout << "  <crop-x> <crop-y> <crop-w> <crop-h>  Crop parameters" << std::endl;
    std::cout << "  --rotate <degrees>              Rotate output image by angle in degrees (counterclockwise)" << std::endl;
    std::cout << "  --flip <axis>                   Flip output image. Axis: 0=Vertical, 1=Horizontal, 2=Both" << std::endl;
}

int main(int argc, char *argv[])
{
    if (argc < 6) {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }
    
    // Parse basic arguments
    fs::path vol_path = argv[1];
    const char *tgt_ptn = argv[2];
    fs::path seg_path = argv[3];
    float tgt_scale = atof(argv[4]);
    int group_idx = atoi(argv[5]);
    
    int num_slices = 1;
    int arg_idx = 6;
    
    // Check if we have num_slices
    if (argc > arg_idx && std::string(argv[arg_idx]).find("--") != 0) {
        num_slices = atoi(argv[arg_idx]);
        arg_idx++;
    }
    
    // Check for crop parameters
    cv::Rect crop;
    bool has_crop = false;
    if (argc > arg_idx + 3 && 
        std::string(argv[arg_idx]).find("--") != 0 &&
        std::string(argv[arg_idx+1]).find("--") != 0 &&
        std::string(argv[arg_idx+2]).find("--") != 0 &&
        std::string(argv[arg_idx+3]).find("--") != 0) {
        crop = {atoi(argv[arg_idx]), atoi(argv[arg_idx+1]), atoi(argv[arg_idx+2]), atoi(argv[arg_idx+3])};
        has_crop = true;
        arg_idx += 4;
    }
    
    // Parse optional transformation parameters
    double rotate_angle = 0.0;
    int flip_axis = -1;
    
    while (arg_idx < argc) {
        std::string arg(argv[arg_idx]);
        if (arg == "--rotate" && arg_idx + 1 < argc) {
            rotate_angle = atof(argv[arg_idx + 1]);
            arg_idx += 2;
        } else if (arg == "--flip" && arg_idx + 1 < argc) {
            flip_axis = atoi(argv[arg_idx + 1]);
            arg_idx += 2;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
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
    if (!has_crop) {
        crop = {0, 0, tgt_size.width, tgt_size.height};
    }
    tgt_size = crop.size();
    
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
        surf->gen(&points, &normals, tgt_size, nullptr, tgt_scale, {-full_size.width/2+crop.x,-full_size.height/2+crop.y,0});
        
        // Calculate the actual mesh centroid
        meshCentroid = calculateMeshCentroid(points);
        globalFlipDecision = shouldFlipNormals(points, normals, meshCentroid);
        orientationDetermined = true;
        
        // Apply the orientation
        applyNormalOrientation(normals, globalFlipDecision);
        
        if (globalFlipDecision) {
            std::cout << "Orienting normals to point consistently (flipped)" << std::endl;
        } else {
            std::cout << "Orienting normals to point consistently (not flipped)" << std::endl;
        }
    }

    cv::Mat_<uint8_t> img;

    float ds_scale = pow(2,-group_idx);
    if (group_idx && !slice_gen) {
        points *= ds_scale;
    }

    if (num_slices == 1) {
        readInterpolated3D(img, ds.get(), points, &chunk_cache);
        
        // Apply transformations
        if (std::abs(rotate_angle) > 1e-6) {
            rotateImage(img, rotate_angle);
        }
        if (flip_axis >= 0) {
            flipImage(img, flip_axis);
        }
        
        cv::imwrite(tgt_ptn, img);
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
                    surf->gen(&points, &normals, {w,crop.height}, nullptr, tgt_scale, {-full_size.width/2+x,-full_size.height/2+crop.y,0});
                    
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
                readInterpolated3D(img, ds.get(), points+off*ds_scale*normals, &chunk_cache);
            }
            
            // Apply transformations
            if (std::abs(rotate_angle) > 1e-6) {
                rotateImage(img, rotate_angle);
            }
            if (flip_axis >= 0) {
                flipImage(img, flip_axis);
            }
            
            snprintf(buf, 1024, tgt_ptn, i);
            cv::imwrite(buf, img);
        }
    }

    delete surf;

    return EXIT_SUCCESS;
}
