#include <nlohmann/json.hpp>

#include <xtensor/xarray.hpp>
#include <xtensor/xaxis_slice_iterator.hpp>
#include <xtensor/xio.hpp>
#include <xtensor/xbuilder.hpp>
#include <xtensor/xview.hpp>

#include "z5/factory.hxx"
#include "z5/filesystem/handle.hxx"
#include "z5/filesystem/dataset.hxx"
#include "z5/common.hxx"
#include "z5/multiarray/xtensor_access.hxx"
#include "z5/attributes.hxx"

#include <opencv2/highgui.hpp>
#include <opencv2/core.hpp>

#include "vc/core/util/Slicing.hpp"

#include <unordered_map>

using shape = z5::types::ShapeType;
using namespace xt::placeholders;

std::ostream& operator<< (std::ostream& out, const std::vector<size_t> &v) {
    if ( !v.empty() ) {
        out << '[';
        for(auto &v : v)
            out << v << ",";
        out << "\b]"; // use ANSI backspace character '\b' to overwrite final ", "
    }
    return out;
}

std::ostream& operator<< (std::ostream& out, const std::vector<int> &v) {
    if ( !v.empty() ) {
        out << '[';
        for(auto &v : v)
            out << v << ",";
        out << "\b]"; // use ANSI backspace character '\b' to overwrite final ", "
    }
    return out;
}

template <size_t N>
std::ostream& operator<< (std::ostream& out, const std::array<size_t,N> &v) {
    if ( !v.empty() ) {
        out << '[';
        for(auto &v : v)
            out << v << ",";
        out << "\b]"; // use ANSI backspace character '\b' to overwrite final ", "
    }
    return out;
}

std::ostream& operator<< (std::ostream& out, const xt::svector<size_t> &v) {
    if ( !v.empty() ) {
        out << '[';
        for(auto &v : v)
            out << v << ",";
        out << "\b]"; // use ANSI backspace character '\b' to overwrite final ", "
    }
    return out;
}


shape chunkId(const std::unique_ptr<z5::Dataset> &ds, shape coord)
{
    shape div = ds->chunking().blockShape();
    shape id = coord;
    for(int i=0;i<id.size();i++)
        id[i] /= div[i];
    return id;
}

shape idCoord(const std::unique_ptr<z5::Dataset> &ds, shape id)
{
    shape mul = ds->chunking().blockShape();
    shape coord = id;
    for(int i=0;i<coord.size();i++)
        coord[i] *= mul[i];
    return coord;
}

void timed_plane_slice(PlaneCoords &plane, z5::Dataset *ds, size_t size, ChunkCache *cache, std::string msg)
{
    xt::xarray<float> coords;
    xt::xarray<uint8_t> img;
    
    auto start = std::chrono::high_resolution_clock::now();
    plane.gen_coords(coords, size, size);
    auto end = std::chrono::high_resolution_clock::now();
    // std::cout << std::chrono::duration<double>(end-start).count() << "s gen_coords() " << msg << std::endl;
    start = std::chrono::high_resolution_clock::now();
    readInterpolated3D(img, ds, coords, cache);
    end = std::chrono::high_resolution_clock::now();
    std::cout << std::chrono::duration<double>(end-start).count() << "s slicing " << msg << std::endl; 
}

int main(int argc, char *argv[])
{
  assert(argc == 2);
  // z5::filesystem::handle::File f(argv[1]);
  z5::filesystem::handle::Group group(argv[1], z5::FileMode::FileMode::r);
  z5::filesystem::handle::Dataset ds_handle(group, "1", "/");
  std::unique_ptr<z5::Dataset> ds = z5::filesystem::openDataset(ds_handle);

  std::cout << "ds shape " << ds->shape() << std::endl;
  std::cout << "ds shape via chunk " << ds->chunking().shape() << std::endl;
  std::cout << "chunk shape shape " << ds->chunking().blockShape() << std::endl;

  xt::xarray<float> coords = xt::zeros<float>({500,500,3});
  xt::xarray<uint8_t> img;
  
  std::vector<cv::Mat> chs;
  cv::imreadmulti("../grid_slice_coords.tif", chs, cv::IMREAD_UNCHANGED);
  cv::Mat_<cv::Vec3f> points;
  cv::merge(chs, points);
  
  GridCoords gen_grid(&points);
  
  PlaneCoords gen_plane({2000,2000,2000},{0.5,0.5,0.5});
  // PlaneCoords gen_plane({2000,2000,2000},{0.0,0.0,1.0});
  // PlaneCoords gen_plane({0,0,0},{0.0,0.0,1.0});
  // PlaneCoords gen_plane({2000,2000,0},{0.0,0.0,1.0});
  
  PlaneCoords plane_x({2000,2000,2000},{1.0,0.0,0.0});
  PlaneCoords plane_y({2000,2000,2000},{0.0,1.0,0.0});
  PlaneCoords plane_z({2000,2000,2000},{0.0,0.0,1.0});
  
  // gen_plane.gen_coords(coords, 1000, 1000);
  gen_grid.gen_coords(coords, 0, 0, 1000, 1000, 1.0, 0.5);

  ChunkCache chunk_cache(10e9);
  
  auto start = std::chrono::high_resolution_clock::now();
  readInterpolated3D(img,ds.get(),coords, &chunk_cache);
  auto end = std::chrono::high_resolution_clock::now();
  std::cout << std::chrono::duration<double>(end-start).count() << "s cold" << std::endl;
  
  cv::Mat m = cv::Mat(img.shape(0), img.shape(1), CV_8U, img.data());
  cv::imwrite("plane.tif", m);
  
  for(int r=0;r<10;r++) {
    start = std::chrono::high_resolution_clock::now();
    readInterpolated3D(img,ds.get(),coords, &chunk_cache);
    end = std::chrono::high_resolution_clock::now();
    std::cout << std::chrono::duration<double>(end-start).count() << "s cached" << std::endl;
  }
  
  std::cout << "testing different slice directions / caching" << std::endl;
  for(int r=0;r<3;r++) {
      timed_plane_slice(plane_x, ds.get(), 4000, &chunk_cache, "yz cold");
      timed_plane_slice(plane_x, ds.get(), 4000, &chunk_cache, "yz");
      timed_plane_slice(plane_y, ds.get(), 4000, &chunk_cache, "xz cold");
      timed_plane_slice(plane_y, ds.get(), 4000, &chunk_cache, "xz");
      timed_plane_slice(plane_z, ds.get(), 4000, &chunk_cache, "xy cold");
      timed_plane_slice(plane_z, ds.get(), 4000, &chunk_cache, "xy");
      timed_plane_slice(gen_plane, ds.get(), 4000, &chunk_cache, "diag cold");
      timed_plane_slice(gen_plane, ds.get(), 4000, &chunk_cache, "diag");
  }
  
  
  // readInterpolated3D(img,ds.get(),coords);
  // m = cv::Mat(img.shape(0), img.shape(1), CV_8U, img.data());
  // cv::imwrite("plane.tif", m);
  
  
  // m = cv::Mat(coords.shape(0), coords.shape(1), CV_32FC3, coords.data());
  // std::vector<cv::Mat> chs;
  // cv::split(m, chs);
  // cv::imwrite("coords_x.tif", chs[2]);
  // cv::imwrite("coords_y.tif", chs[1]);
  // cv::imwrite("coords_z.tif", chs[0]);
    
  return 0;
}
