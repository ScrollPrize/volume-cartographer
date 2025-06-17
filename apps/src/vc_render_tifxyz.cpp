#include "vc/core/util/Slicing.hpp"
#include "vc/core/util/Surface.hpp"
#include "vc/core/types/ChunkedTensor.hpp"

#include "z5/factory.hxx"
#include <nlohmann/json.hpp>

#include <opencv2/highgui.hpp>

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

using json = nlohmann::json;

/**
 * @brief Structure to hold affine transform data
 */
struct AffineTransform {
    cv::Mat_<float> matrix;  // 3x4 matrix in ZYX format
    cv::Vec3f offset;        // optional pre-transform offset
    bool hasOffset;
    
    AffineTransform() : hasOffset(false), offset(0, 0, 0) {
        matrix = cv::Mat_<float>::eye(3, 4);
    }
};

/**
 * @brief Load affine transform from file (JSON or text format)
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
    
    // Try to parse as JSON first
    try {
        json j;
        file >> j;
        
        if (j.contains("affine")) {
            auto affine = j["affine"];
            if (affine.size() != 3) {
                throw std::runtime_error("Affine matrix must have 3 rows");
            }
            
            transform.matrix = cv::Mat_<float>(3, 4);
            for (int i = 0; i < 3; i++) {
                if (affine[i].size() != 4) {
                    throw std::runtime_error("Each row of affine matrix must have 4 elements");
                }
                for (int j = 0; j < 4; j++) {
                    transform.matrix(i, j) = affine[i][j].get<float>();
                }
            }
        }
        
        if (j.contains("offset")) {
            auto offset = j["offset"];
            if (offset.size() != 3) {
                throw std::runtime_error("Offset must have 3 elements");
            }
            transform.offset = cv::Vec3f(offset[0].get<float>(), 
                                        offset[1].get<float>(), 
                                        offset[2].get<float>());
            transform.hasOffset = true;
        }
    } catch (json::parse_error&) {
        // Not JSON, try plain text format
        file.clear();
        file.seekg(0);
        
        std::vector<float> values;
        float val;
        while (file >> val) {
            values.push_back(val);
        }
        
        if (values.size() != 12 && values.size() != 15) {
            throw std::runtime_error("Text file must contain 12 values (3x4 matrix) or 15 values (3x4 matrix + 3 offset values)");
        }
        
        // Load the 3x4 matrix
        transform.matrix = cv::Mat_<float>(3, 4);
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                transform.matrix(i, j) = values[i * 4 + j];
            }
        }
        
        // Load offset if present
        if (values.size() == 15) {
            transform.offset = cv::Vec3f(values[12], values[13], values[14]);
            transform.hasOffset = true;
        }
    }
    
    return transform;
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
            
            // Apply optional offset first
            if (transform.hasOffset) {
                pt += transform.offset;
            }
            
            // Apply affine transform (note: matrix is in ZYX format as per the Rust example)
            float px = pt[0];
            float py = pt[1];
            float pz = pt[2];
            
            // Row 0 (Z in output)
            float z_new = transform.matrix(0, 2) * px + transform.matrix(0, 1) * py + 
                         transform.matrix(0, 0) * pz + transform.matrix(0, 3);
            // Row 1 (Y in output) 
            float y_new = transform.matrix(1, 2) * px + transform.matrix(1, 1) * py + 
                         transform.matrix(1, 0) * pz + transform.matrix(1, 3);
            // Row 2 (X in output)
            float x_new = transform.matrix(2, 2) * px + transform.matrix(2, 1) * py + 
                         transform.matrix(2, 0) * pz + transform.matrix(2, 3);
            
            pt[0] = x_new;
            pt[1] = y_new;
            pt[2] = z_new;
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
            float nz_new = transform.matrix(0, 2) * nx + transform.matrix(0, 1) * ny + 
                          transform.matrix(0, 0) * nz;
            float ny_new = transform.matrix(1, 2) * nx + transform.matrix(1, 1) * ny + 
                          transform.matrix(1, 0) * nz;
            float nx_new = transform.matrix(2, 2) * nx + transform.matrix(2, 1) * ny + 
                          transform.matrix(2, 0) * nz;
            
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
 * @brief Orient normals to point outward from a reference point
 * 
 * @param points Matrix of 3D points (cv::Mat_<cv::Vec3f>)
 * @param normals Matrix of normal vectors to reorient (modified in-place)
 * @param referencePoint Optional external reference point (default: compute centroid)
 * @param usePointsForCentroid Whether to compute centroid from points or use referencePoint
 * @return bool True if normals were flipped, false otherwise
 */
bool orientNormals(
    const cv::Mat_<cv::Vec3f>& points, 
    cv::Mat_<cv::Vec3f>& normals,
    const cv::Vec3f& referencePoint = cv::Vec3f(0,0,0),
    bool usePointsForCentroid = true)
{
    cv::Vec3f refPt = referencePoint;
    if (usePointsForCentroid) {
        refPt = cv::Vec3f(0, 0, 0);
        int validPoints = 0;
        for (int y = 0; y < points.rows; y++) {
            for (int x = 0; x < points.cols; x++) {
                const cv::Vec3f& pt = points(y, x);
                if (std::isnan(pt[0]) || std::isnan(pt[1]) || std::isnan(pt[2])) {
                    continue;
                }
                refPt += pt;
                validPoints++;
            }
        }
        if (validPoints > 0) {
            refPt /= static_cast<float>(validPoints);
        }
    }

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
            
            cv::Vec3f direction = refPt - pt;
            float distance = cv::norm(direction);
            
            if (distance < 1e-6) {
                continue;
            }
            
            direction /= distance;
            
            float dotProduct = direction.dot(n);
            if (dotProduct > 0) {
                pointingToward++;
            } else {
                pointingAway++;
            }
        }
    }
    
    bool shouldFlip = pointingAway > pointingToward;
    
    if (shouldFlip) {
        for (int y = 0; y < normals.rows; y++) {
            for (int x = 0; x < normals.cols; x++) {
                cv::Vec3f& n = normals(y, x);
                if (std::isnan(n[0]) || std::isnan(n[1]) || std::isnan(n[2])) {
                    continue;
                }
                n = -n;
            }
        }
    }
    
    return shouldFlip;
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
    if (argc != 6 && argc != 7 && argc != 8 && argc != 11 && argc != 12) {
        std::cout << "usage: " << argv[0] << " <ome-arr-volume> <output> <seg-path> <tgt-scale> <ome-zarr-group-idx> [affine-transform-file]" << std::endl;
        std::cout << "or: " << argv[0] << " <ome-zarr-volume> <ptn> <seg-path> <tgt-scale> <ome-zarr-group-idx> <num-slices> [affine-transform-file]" << std::endl;
        std::cout << "or: " << argv[0] << " <ome-zarr-volume> <ptn> <seg-path> <tgt-scale> <ome-zarr-group-idx> <num-slices> <crop-x> <crop-y> <crop-w> <crop-h> [affine-transform-file]" << std::endl;
        return EXIT_SUCCESS;
    }

    fs::path vol_path = argv[1];
    const char *tgt_ptn = argv[2];
    fs::path seg_path = argv[3];
    float tgt_scale = atof(argv[4]);
    int group_idx = atoi(argv[5]);
    
    int num_slices = 1;
    if (argc >= 7 && argc != 8) // 7, 11, or 12
        num_slices = atoi(argv[6]);
    
    // Load affine transform if provided
    AffineTransform affineTransform;
    bool hasAffine = false;
    
    // Check for affine file in different argument positions
    std::string affineFile;
    if (argc == 7 && num_slices == 1) {
        // Single slice with affine: 6 args + affine
        affineFile = argv[6];
        num_slices = 1; // Reset since this is actually the affine file
    } else if (argc == 8) {
        // Multi-slice with affine: 7 args + affine
        affineFile = argv[7];
    } else if (argc == 12) {
        // Crop with affine: 11 args + affine
        affineFile = argv[11];
    }
    
    if (!affineFile.empty()) {
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
    
    if (argc == 11 || argc == 12) {
        crop = {atoi(argv[7]),atoi(argv[8]),atoi(argv[9]),atoi(argv[10])};
        tgt_size = crop.size();
    }        
    
    std::cout << "rendering size " << tgt_size << " at scale " << tgt_scale << " crop " << crop << std::endl;
    
    cv::Mat_<cv::Vec3f> points, normals;
    
    bool slice_gen = false;
    
    if (tgt_size.width >= 10000 && num_slices > 1)
        slice_gen = true;
    else {
        surf->gen(&points, &normals, tgt_size, nullptr, tgt_scale, {-full_size.width/2+crop.x,-full_size.height/2+crop.y,0});
        
        bool flipped = orientNormals(points, normals);
        if (flipped) {
            std::cout << "Flipping normals" << std::endl;
        }
        
        // Apply affine transform if provided
        if (hasAffine) {
            applyAffineTransform(points, normals, affineTransform);
        }
    }

    cv::Mat_<uint8_t> img;

    float ds_scale = pow(2,-group_idx);
    if (group_idx && !slice_gen) {
        points *= ds_scale;
    }

    if (num_slices == 1) {
        readInterpolated3D(img, ds.get(), points, &chunk_cache);
        cv::imwrite(tgt_ptn, img);
    }
    else {
        char buf[1024];
        for(int i=0;i<num_slices;i++) {
            float off = i-num_slices/2;
            if (slice_gen) {
                img.create(tgt_size);
                for(int x=crop.x;x<crop.x+crop.width;x+=1024) {
                    int w = std::min(tgt_size.width+crop.x-x, 1024);
                    surf->gen(&points, &normals, {w,crop.height}, nullptr, tgt_scale, {-full_size.width/2+x,-full_size.height/2+crop.y,0});
                    
                    orientNormals(points, normals);
                    
                    // Apply affine transform if provided
                    if (hasAffine) {
                        applyAffineTransform(points, normals, affineTransform);
                    }
                    
                    cv::Mat_<uint8_t> slice;
                    readInterpolated3D(slice, ds.get(), points*ds_scale+off*normals*ds_scale, &chunk_cache);
                    slice.copyTo(img(cv::Rect(x-crop.x,0,w,crop.height)));
                }
            }
            else {
                cv::Mat_<cv::Vec3f> offsetPoints = points + off*ds_scale*normals;
                // Apply affine transform if provided (for non-slice_gen case)
                if (hasAffine && !slice_gen) {
                    cv::Mat_<cv::Vec3f> offsetNormals = normals.clone();
                    applyAffineTransform(offsetPoints, offsetNormals, affineTransform);
                }
                readInterpolated3D(img, ds.get(), offsetPoints, &chunk_cache);
            }
            snprintf(buf, 1024, tgt_ptn, i);
            cv::imwrite(buf, img);
        }
    }

    delete surf;

    return EXIT_SUCCESS;
}
