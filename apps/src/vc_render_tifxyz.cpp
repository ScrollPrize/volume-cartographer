#include "vc/core/util/Slicing.hpp"
#include "vc/core/util/Surface.hpp"
#include "vc/core/types/ChunkedTensor.hpp"

#include "z5/factory.hxx"
#include <nlohmann/json.hpp>

#include <opencv2/highgui.hpp>

namespace fs = std::filesystem;

using json = nlohmann::json;

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
    if (argc != 6 && argc != 7 && argc != 11) {
        std::cout << "usage: " << argv[0] << " <ome-arr-volume> <output> <seg-path> <tgt-scale> <ome-zarr-group-idx>" << std::endl;
        std::cout << "or: " << argv[0] << " <ome-zarr-volume> <ptn> <seg-path> <tgt-scale> <ome-zarr-group-idx> <num-slices>" << std::endl;
        std::cout << "or: " << argv[0] << " <ome-zarr-volume> <ptn> <seg-path> <tgt-scale> <ome-zarr-group-idx> <num-slices> <crop-x> <crop-y> <crop-w> <crop-h>" << std::endl;
        return EXIT_SUCCESS;
    }

    fs::path vol_path = argv[1];
    const char *tgt_ptn = argv[2];
    fs::path seg_path = argv[3];
    float tgt_scale = atof(argv[4]);
    int group_idx = atoi(argv[5]);
    
    int num_slices = 1;
    if (argc == 7 || argc == 11)
        num_slices = atoi(argv[6]);

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

    cv::Mat_<cv::Vec3f> raw_points = surf->rawPoints();
    for(int j=0;j<raw_points.rows;j++)
        for(int i=0;i<raw_points.cols;i++)
            if (raw_points(j,i)[0] == -1)
                raw_points(j,i) = {NAN,NAN,NAN};
    surf->setRawPoints(raw_points);
    
    cv::Size full_size = raw_points.size();
    full_size.width *= tgt_scale/surf->_scale[0];
    full_size.height *= tgt_scale/surf->_scale[1];
    
    cv::Size tgt_size = full_size;
    cv::Rect crop = {0,0,tgt_size.width, tgt_size.height};
    
    if (argc == 11) {
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
                    
                    cv::Mat_<uint8_t> slice;
                    readInterpolated3D(slice, ds.get(), points*ds_scale+off*normals*ds_scale, &chunk_cache);
                    slice.copyTo(img(cv::Rect(x-crop.x,0,w,crop.height)));
                }
            }
            else {
                readInterpolated3D(img, ds.get(), points+off*ds_scale*normals, &chunk_cache);
            }
            snprintf(buf, 1024, tgt_ptn, i);
            cv::imwrite(buf, img);
        }
    }


    return EXIT_SUCCESS;
}
