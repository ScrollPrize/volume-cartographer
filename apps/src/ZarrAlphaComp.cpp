#include <nlohmann/json.hpp>

#include "vc/core/util/xtensor_include.hpp"
#include XTENSORINCLUDE(containers, xarray.hpp)
#include XTENSORINCLUDE(io, xio.hpp)
#include XTENSORINCLUDE(generators, xbuilder.hpp)
#include XTENSORINCLUDE(views, xview.hpp)

#include "z5/factory.hxx"
#include "z5/filesystem/handle.hxx"
#include "z5/multiarray/xtensor_access.hxx"
#include "z5/attributes.hxx"

#include <opencv2/highgui.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "vc/core/util/Slicing.hpp"
#include "vc/core/io/PointSetIO.hpp"


using shape = z5::types::ShapeType;
using namespace xt::placeholders;


xt::xarray<float> xt_from_mat(const cv::Mat_<cv::Vec3f> &m)
{
    xt::xarray<float> t = xt::empty<float>({m.rows, m.cols, 3});
    
    for(int j=0;j<m.rows;j++) 
        for(int i=0;i<m.cols;i++) {
            cv::Vec3f v = m(j,i);
            t(j,i,0) = v[2];
            t(j,i,1) = v[1];
            t(j,i,2) = v[0];
        }
        
        return t;
}

cv::Mat_<cv::Vec3f> surf_alpha_integ_dbg(z5::Dataset *ds, ChunkCache *chunk_cache, const cv::Mat_<cv::Vec3f> &points, const cv::Mat_<cv::Vec3f> &normals)
{
    cv::Mat_<cv::Vec3f> res;
    
    cv::Mat_<float> integ(points.size(), 0);
    cv::Mat_<float> integ_blur(points.size(), 0);
    cv::Mat_<float> transparent(points.size(), 1);
    cv::Mat_<float> blur(points.size(), 0);
    cv::Mat_<float> integ_z(points.size(), 0);
    
    for(int n=0;n<21;n++) {
        xt::xarray<uint8_t> raw_extract;
        // coords = points_reg*2.0;
        float off = (n-5)*0.5;
        readInterpolated3D(raw_extract, ds, xt_from_mat((points+normals*off)*0.5), chunk_cache);
        cv::Mat_<uint8_t> slice = cv::Mat(raw_extract.shape(0), raw_extract.shape(1), CV_8U, raw_extract.data());
        
        // char buf[64];
        // sprintf(buf, "slice%02d.tif", n);
        // cv::imwrite(buf, slice);
        
        cv::Mat floatslice;
        slice.convertTo(floatslice, CV_32F, 1/255.0);
        
        cv::GaussianBlur(floatslice, blur, {7,7}, 0);
        cv::Mat opaq_slice = blur;
        
        float low = 0.0; //map to 0
        float up = 1.0; //map to 1
        opaq_slice = (opaq_slice-low)/(up-low);
        opaq_slice = cv::min(opaq_slice,1);
        opaq_slice = cv::max(opaq_slice,0);
        
        // sprintf(buf, "opaq%02d.tif", n);
        // cv::imwrite(buf, opaq_slice);
        
        printf("vals %d i t o b v: %f %f %f %f\n", n, integ.at<float>(500,600), transparent.at<float>(500,600), opaq_slice.at<float>(500,600), blur.at<float>(500,600), floatslice.at<float>(500,600));
        
        cv::Mat joint = transparent.mul(opaq_slice);
        integ += joint.mul(floatslice);
        integ_blur += joint.mul(blur);
        integ_z += joint * off;
        transparent = transparent-joint;
        
        // sprintf(buf, "transp%02d.tif", n);
        // cv::imwrite(buf, transparent);
        // 
        // sprintf(buf, "opaq2%02d.tif", n);
        // cv::imwrite(buf, opaq_slice);
        
        printf("res %d i t: %f %f\n", n, integ.at<float>(500,600), transparent.at<float>(500,600));
        
        // avgimg = avgimg + floatslice;
        // cv::imwrite(buf, avgimg/(n+1));
        
        // slices.push_back(slice);
        // for(int j=0;j<points.rows;j++)
        //     for(int i=0;i<points.cols;i++) {
        //         //found == 0: still searching for first time < 50!
        //         //found == 1: record < 50 start looking for >= 50 to stop
        //         //found == 2: done, found border
        //         if (slice(j,i) < 40 && found(j,i) <= 1) {
        //             height(j,i) = n+1;
        //             found(j,i) = 1;
        //         }
        //         else if (slice(j,i) >= 40 && found(j,i) == 1) {
        //             found(j,i) = 2;
        //         }
        //     }
    }        // slices.push_back(slice);
    
    integ /= (1-transparent);
    integ_blur /= (1-transparent);
    integ_z /= (1-transparent);
    
    cv::imwrite("blended.tif", integ);
    cv::imwrite("blended_blur.tif", integ_blur);
    cv::imwrite("blended_comp1.tif", integ/(integ_blur+0.5));
    cv::imwrite("blended_comp3.tif", integ-integ_blur+0.5);
    cv::imwrite("blended_comp2.tif", integ/(integ_blur+0.01));
    cv::imwrite("tranparency.tif", transparent);
    
    // for(int j=0;j<points.rows;j++)
    //     for(int i=0;i<points.cols;i++)
    //         if (found(j,i) == 1)
    //             height(j,i) = 0;
    
    //never change opencv, never change ...

    // cv::cvtColor(height, mul, cv::COLOR_GRAY2BGR);
    // cv::imwrite("max.tif", maximg);

    cv::Mat mul;
    cv::cvtColor(integ_z, mul, cv::COLOR_GRAY2BGR);
    cv::Mat_<cv::Vec3f> new_surf = points + normals.mul(mul);
    cv::Mat_<cv::Vec3f> new_surf_1 = new_surf + normals;
    cv::Mat_<cv::Vec3f> new_surf_n1 = new_surf - normals;
//     
    xt::xarray<uint8_t> img;
    readInterpolated3D(img, ds, xt_from_mat(new_surf*0.5), chunk_cache);
    cv::Mat_<uint8_t> slice = cv::Mat(img.shape(0), img.shape(1), CV_8U, img.data());
//     
    printf("writ slice!\n");
    cv::imwrite("new_surf.tif", slice);
    
    readInterpolated3D(img, ds, xt_from_mat(new_surf_1*0.5), chunk_cache);
    slice = cv::Mat(img.shape(0), img.shape(1), CV_8U, img.data());
    cv::imwrite("new_surf1.tif", slice);
    
    readInterpolated3D(img, ds, xt_from_mat(new_surf_n1*0.5), chunk_cache);
    slice = cv::Mat(img.shape(0), img.shape(1), CV_8U, img.data());
    cv::imwrite("new_surf-1.tif", slice);
    
    // cv::Mat_<float> height_vis = height/21;
    // height_vis = cv::min(height_vis,1-height_vis)*2;
    // cv::imwrite("off.tif", height_vis);
    
    //now big question: how far away from average is the new surf!
    
//     cv::Mat avg_surf;
//     cv::GaussianBlur(new_surf, avg_surf, {7,7}, 0);
//     
//     readInterpolated3D(img, ds, xt_from_mat(avg_surf*0.5), chunk_cache);
//     slice = cv::Mat(img.shape(0), img.shape(1), CV_8U, img.data());
//     
//     cv::imwrite("avg_surf.tif", slice);
//     
//     
//     cv::Mat_<float> rel_height(points.size(), 0);
//     
//     cv::Mat_<cv::Vec3f> dist = avg_surf-new_surf;
//     
//     #pragma omp parallel for
//     for(int j=0;j<points.rows;j++)
//         for(int i=0;i<points.cols;i++) {
//             rel_height(j,i) = cv::norm(dist(j,i));
//         }
//         
//         cv::imwrite("rel_height.tif", rel_height);
    
    return new_surf;
}

class LifeTime
{
public:
    LifeTime(std::string msg)
    {
        std::cout << msg << std::flush;
        start = std::chrono::high_resolution_clock::now();
    }
    ~LifeTime()
    {
        auto end = std::chrono::high_resolution_clock::now();
        std::cout << " took " << std::chrono::duration<double>(end-start).count() << " s" << std::endl;
    }
private:
    std::chrono::time_point<std::chrono::high_resolution_clock> start;
};

cv::Mat_<cv::Vec3f> surf_alpha_integ(z5::Dataset *ds, ChunkCache *chunk_cache, const cv::Mat_<cv::Vec3f> &points, const cv::Mat_<cv::Vec3f> &normals, cv::Mat *composed)
{
    cv::Mat_<cv::Vec3f> res;
    
    cv::Mat_<float> integ(points.size(), 0);
    cv::Mat_<float> integ_blur(points.size(), 0);
    cv::Mat_<float> transparent(points.size(), 1);
    cv::Mat_<float> blur(points.size(), 0);
    cv::Mat_<float> integ_z(points.size(), 0);
    
    for(int n=0;n<21;n++) {
        xt::xarray<uint8_t> raw_extract;
        // coords = points_reg*2.0;
        float off = (n-5)*0.5;
        readInterpolated3D(raw_extract, ds, xt_from_mat((points+normals*off)*0.5), chunk_cache);
        cv::Mat_<uint8_t> slice = cv::Mat(raw_extract.shape(0), raw_extract.shape(1), CV_8U, raw_extract.data());
        
        cv::Mat floatslice;
        slice.convertTo(floatslice, CV_32F, 1/255.0);
        
        cv::GaussianBlur(floatslice, blur, {7,7}, 0);
        cv::Mat opaq_slice = blur;
        
        float low = 0.0; //map to 0
        float up = 1.0; //map to 1
        opaq_slice = (opaq_slice-low)/(up-low);
        opaq_slice = cv::min(opaq_slice,1);
        opaq_slice = cv::max(opaq_slice,0);
        
        cv::Mat joint = transparent.mul(opaq_slice);
        integ += joint.mul(floatslice);
        integ_blur += joint.mul(blur);
        integ_z += joint * off;
        transparent = transparent-joint;
    }
    
    
    if (composed) {
        integ /= (1-transparent);
        integ_blur /= (1-transparent);
        *composed = integ/(integ_blur+0.5);
    }    

    cv::Mat mul;
    cv::cvtColor(integ_z, mul, cv::COLOR_GRAY2BGR);
    cv::Mat_<cv::Vec3f> new_surf = points + normals.mul(mul);
    
    return new_surf;
}

void writeArea(GridCoords &grid, int w, int h, z5::Dataset *ds, ChunkCache *cache, std::string path, float offset = 0.0)
{
    cv::Mat m;
    xt::xarray<uint8_t> img;
    xt::xarray<float> coords;
    
    grid.setOffsetZ(offset);
    grid.gen_coords(coords, 0, 0, w, h, 1.0, 0.5);
    readInterpolated3D(img, ds, coords, cache);
    m = cv::Mat(img.shape(0), img.shape(1), CV_8U, img.data());
    cv::imwrite(path, m);
}

void writeArea(cv::Mat_<cv::Vec3f> &points, int w, int h, z5::Dataset *ds, ChunkCache *cache, std::string path, float offset = 0.0)
{
    GridCoords gen_grid(&points);
    writeArea(gen_grid, w, h, ds, cache, path, offset);
}

int main(int argc, char *argv[])
{
    if (argc != 7) {
        printf("usage: tool volume.zarr segment.vcps x y w h\n");
        return EXIT_SUCCESS;
    }
    
    const char *vol_path = argv[1];
    const char *segment_path = argv[2];
    
    int x = atoi(argv[3]);
    int y = atoi(argv[4]);
    int w = atoi(argv[5]);
    int h = atoi(argv[6]);
    
    printf("reading area %d %d %d %d\n", x,y,w,h);
    
    assert(w*h > 1);
    
    // int x = 1000;
    // int y = 900;
    // int w = 1200;
    // int h = 600;
    
    z5::filesystem::handle::Group group(vol_path, z5::FileMode::FileMode::r);
    z5::filesystem::handle::Dataset ds_handle(group, "1", "/");
    std::unique_ptr<z5::Dataset> ds = z5::filesystem::openDataset(ds_handle);

    auto timer = new LifeTime("reading segment ...");
    volcart::OrderedPointSet<cv::Vec3d> segment_raw = volcart::PointSetIO<cv::Vec3d>::ReadOrderedPointSet(segment_path);
    delete timer;
    
    timer = new LifeTime("smoothing segment ...");
    cv::Mat src(segment_raw.height(), segment_raw.width(), CV_64FC3, (void*)const_cast<cv::Vec3d*>(&segment_raw[0]));
    
    cv::Mat_<cv::Vec3f> points;
    src.convertTo(points, CV_32F);
    
    points = smooth_vc_segmentation(points);
    delete timer;
    
    timer = new LifeTime("calculating normals ...");
    cv::Mat_<cv::Vec3f> normals = vc_segmentation_calc_normals(points);
    delete timer;
    
    ChunkCache chunk_cache(10e9);
    
    double sx, sy;
    vc_segmentation_scales(points, sx, sy);
    
    cv::Rect roi(2*x*sx,2*y*sy,2*w*sx,2*h*sy);
    points = points(roi);
    normals = normals(roi);
    
    cv::resize(points, points, {0,0}, 1/sx, 1/sy);
    cv::resize(normals, normals, {0,0}, 1/sx, 1/sy);
    
    writeArea(points, w, h, ds.get(), &chunk_cache, "original.tif");
    
    cv::Mat composed;
    
    for(int i=0;i<3;i++) {
        LifeTime time("surface alpha integration");
        //FIXME use sx sy!
        points = surf_alpha_integ(ds.get(), &chunk_cache, points, normals, &composed);
    }
    
    cv::imwrite("alpha_composed.tif", composed);
    GridCoords gen_grid(&points);
    
    writeArea(gen_grid, w, h, ds.get(), &chunk_cache, "alpha_optimized.tif");
    writeArea(gen_grid, w, h, ds.get(), &chunk_cache, "alpha_optimized+1.tif", 1.0);
    writeArea(gen_grid, w, h, ds.get(), &chunk_cache, "alpha_optimized+2.tif", 2.0);
    writeArea(gen_grid, w, h, ds.get(), &chunk_cache, "alpha_optimized-1.tif", -1.0);
    writeArea(gen_grid, w, h, ds.get(), &chunk_cache, "alpha_optimized-2.tif", -2.0);
}
