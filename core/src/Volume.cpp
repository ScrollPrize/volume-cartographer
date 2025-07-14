#include "vc/core/types/Volume.hpp"

#include <iomanip>
#include <sstream>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "z5/attributes.hxx"
#include "z5/dataset.hxx"
#include "z5/filesystem/handle.hxx"
#include "z5/metadata.hxx"
#include "z5/handle.hxx"
#include "z5/types/types.hxx"
#include "z5/util/util.hxx"
#include "z5/util/blocking.hxx"
#include "z5/util/format_data.hxx"
#include "z5/factory.hxx"
#include "z5/multiarray/xtensor_access.hxx"

#include "xtensor/xarray.hpp"

#include "vc/core/util/Slicing.hpp"

namespace fs = volcart::filesystem;

using namespace volcart;

// Load a Volume from disk
Volume::Volume(fs::path path) : DiskBasedObjectBaseClass(std::move(path))
{
    if (metadata_.get<std::string>("type") != "vol") {
        throw std::runtime_error("File not of type: vol");
    }

    width_ = metadata_.get<int>("width");
    height_ = metadata_.get<int>("height");
    slices_ = metadata_.get<int>("slices");
    numSliceCharacters_ = std::to_string(slices_).size();

    std::vector<std::mutex> init_mutexes(slices_);

    slice_mutexes_.swap(init_mutexes);
    
    zarrOpen();
}

// Setup a Volume from a folder of slices
Volume::Volume(fs::path path, std::string uuid, std::string name)
    : DiskBasedObjectBaseClass(
          std::move(path), std::move(uuid), std::move(name)),
          slice_mutexes_(slices_)
{
    metadata_.set("type", "vol");
    metadata_.set("width", width_);
    metadata_.set("height", height_);
    metadata_.set("slices", slices_);
    metadata_.set("voxelsize", double{});
    metadata_.set("min", double{});
    metadata_.set("max", double{});    

    zarrOpen();
}

void Volume::zarrOpen()
{
    if (!metadata_.hasKey("format") || metadata_.get<std::string>("format") != "zarr")
        return;

    isZarr = true;
    zarrFile_ = new z5::filesystem::handle::File(path_);
    z5::filesystem::handle::Group group(path_, z5::FileMode::FileMode::r);
    z5::readAttributes(group, zarrGroup_);
    
    std::vector<std::string> groups;
    zarrFile_->keys(groups);
    std::sort(groups.begin(), groups.end());
    
    //FIXME hardcoded assumption that groups correspond to power-2 scaledowns ...
    for(auto name : groups) {
        z5::filesystem::handle::Dataset ds_handle(group, name, nlohmann::json::parse(std::ifstream(path_/name/".zarray")).value<std::string>("dimension_separator","."));

        zarrDs_.push_back(z5::filesystem::openDataset(ds_handle));
        if (zarrDs_.back()->getDtype() != z5::types::Datatype::uint8 && zarrDs_.back()->getDtype() != z5::types::Datatype::uint16)
            throw std::runtime_error("only uint8 & uint16 is currently supported for zarr datasets incompatible type found in "+path_.string()+" / " +name);
    }
}

// Load a Volume from disk, return a pointer
auto Volume::New(fs::path path) -> Volume::Pointer
{
    return std::make_shared<Volume>(path);
}

// Set a Volume from a folder of slices, return a pointer
auto Volume::New(fs::path path, std::string uuid, std::string name)
    -> Volume::Pointer
{
    return std::make_shared<Volume>(path, uuid, name);
}

auto Volume::sliceWidth() const -> int { return width_; }
auto Volume::sliceHeight() const -> int { return height_; }
auto Volume::numSlices() const -> int { return slices_; }
auto Volume::voxelSize() const -> double
{
    return metadata_.get<double>("voxelsize");
}
auto Volume::min() const -> double { return metadata_.get<double>("min"); }
auto Volume::max() const -> double { return metadata_.get<double>("max"); }

void Volume::setSliceWidth(int w)
{
    width_ = w;
    metadata_.set("width", w);
}

void Volume::setSliceHeight(int h)
{
    height_ = h;
    metadata_.set("height", h);
}

void Volume::setNumberOfSlices(std::size_t numSlices)
{
    slices_ = numSlices;
    numSliceCharacters_ = std::to_string(numSlices).size();
    metadata_.set("slices", numSlices);
}

void Volume::setVoxelSize(double s) { metadata_.set("voxelsize", s); }
void Volume::setMin(double m) { metadata_.set("min", m); }
void Volume::setMax(double m) { metadata_.set("max", m); }

auto Volume::bounds() const -> Volume::Bounds
{
    return {
        {0, 0, 0},
        {static_cast<double>(width_), static_cast<double>(height_),
         static_cast<double>(slices_)}};
}

auto Volume::isInBounds(double x, double y, double z) const -> bool
{
    return x >= 0 && x < width_ && y >= 0 && y < height_ && z >= 0 &&
           z < slices_;
}

auto Volume::isInBounds(const cv::Vec3d& v) const -> bool
{
    return isInBounds(v(0), v(1), v(2));
}

void throw_run_path(const fs::path &path, const std::string msg)
{
    throw std::runtime_error(msg + " for " + path.string());
}

std::ostream& operator<< (std::ostream& out, const xt::xarray<uint8_t>::shape_type &v) {
    if ( !v.empty() ) {
        out << '[';
        for(auto &v : v)
            out << v << ",";
        out << "\b\b]"; // use two ANSI backspace characters '\b' to overwrite final ", "
    }
    return out;
}

z5::Dataset *Volume::zarrDataset(int level)
{
    if (level >= zarrDs_.size())
        return nullptr;

    return zarrDs_[level].get();
}

size_t Volume::numScales()
{
    return zarrDs_.size();
}

cv::Mat Volume::batchInterpolateAt(const cv::Mat_<cv::Vec3f>& coordinates, void* cache) const
{
    cv::Mat_<uint16_t> result(coordinates.size());
    
    if (isZarr && zarrDs_.size() > 0) {
        // Use efficient batch reading for zarr
        cv::Mat_<uint8_t> result8;
        readInterpolated3D(result8, zarrDs_[0].get(), coordinates, static_cast<ChunkCache*>(cache));
        
        // Convert uint8 to uint16 if needed
        if (zarrDs_[0]->getDtype() == z5::types::Datatype::uint8) {
            result8.convertTo(result, CV_16U);
        } else {
            // If dataset is already uint16, we need to handle it differently
            // readInterpolated3D currently only outputs uint8, so we need to convert
            result8.convertTo(result, CV_16U);
        }
    } else {
        // Fallback for non-zarr volumes using single-point interpolation
        for (int i = 0; i < coordinates.rows; i++) {
            for (int j = 0; j < coordinates.cols; j++) {
                const auto& coord = coordinates(i, j);
                
                // For non-zarr volumes, directly perform trilinear interpolation
                // to avoid infinite recursion
                if (!isInBounds(coord[0], coord[1], coord[2])) {
                    result(i, j) = 0;
                    continue;
                }

                // Get the integer coordinates
                int x0 = static_cast<int>(std::floor(coord[0]));
                int y0 = static_cast<int>(std::floor(coord[1]));
                int z0 = static_cast<int>(std::floor(coord[2]));
                
                int x1 = x0 + 1;
                int y1 = y0 + 1;
                int z1 = z0 + 1;

                // Clamp to volume bounds
                x1 = std::min(x1, width_ - 1);
                y1 = std::min(y1, height_ - 1);
                z1 = std::min(z1, slices_ - 1);

                // Get fractional parts
                double fx = coord[0] - x0;
                double fy = coord[1] - y0;
                double fz = coord[2] - z0;

                // Get the 8 neighboring voxel values
                auto v000 = intensityAt(x0, y0, z0);
                auto v001 = intensityAt(x0, y0, z1);
                auto v010 = intensityAt(x0, y1, z0);
                auto v011 = intensityAt(x0, y1, z1);
                auto v100 = intensityAt(x1, y0, z0);
                auto v101 = intensityAt(x1, y0, z1);
                auto v110 = intensityAt(x1, y1, z0);
                auto v111 = intensityAt(x1, y1, z1);

                // Trilinear interpolation
                double v00 = v000 * (1 - fx) + v100 * fx;
                double v01 = v001 * (1 - fx) + v101 * fx;
                double v10 = v010 * (1 - fx) + v110 * fx;
                double v11 = v011 * (1 - fx) + v111 * fx;

                double v0 = v00 * (1 - fy) + v10 * fy;
                double v1 = v01 * (1 - fy) + v11 * fy;

                double res = v0 * (1 - fz) + v1 * fz;

                result(i, j) = static_cast<uint16_t>(std::round(res));
            }
        }
    }
    
    return result;
}

z5::Dataset* Volume::getZarrDatasetAtScale(int scale) const 
{
    if (!isZarr || scale >= static_cast<int>(zarrDs_.size())) {
        return nullptr;
    }
    return zarrDs_[scale].get();
}

std::uint16_t Volume::intensityAt(int x, int y, int z) const
{
    // Bounds checking
    if (x < 0 || x >= width_ || y < 0 || y >= height_ || z < 0 || z >= slices_) {
        return 0;
    }

    if (isZarr && zarrDs_.size() > 0) {
        // For zarr volumes, single voxel reads are inefficient
        // Use interpolateAt instead which uses batch reading
        return interpolateAt(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
    }
    
    // For non-zarr volumes, not implemented
    return 0;
}

std::uint16_t Volume::interpolateAt(double x, double y, double z) const
{
    // For zarr volumes, use batch interpolation for single point
    if (isZarr && zarrDs_.size() > 0) {
        cv::Mat_<cv::Vec3f> coord(1, 1);
        coord(0, 0) = cv::Vec3f(x, y, z);
        
        // Use batch interpolation without cache for single point
        cv::Mat result = batchInterpolateAt(coord, nullptr);
        return result.at<uint16_t>(0, 0);
    }
    
    // For regular volumes, use trilinear interpolation
    // Bounds checking
    if (!isInBounds(x, y, z)) {
        return 0;
    }

    // Get the integer coordinates
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    int z0 = static_cast<int>(std::floor(z));
    
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    int z1 = z0 + 1;

    // Clamp to volume bounds
    x1 = std::min(x1, width_ - 1);
    y1 = std::min(y1, height_ - 1);
    z1 = std::min(z1, slices_ - 1);

    // Get fractional parts
    double fx = x - x0;
    double fy = y - y0;
    double fz = z - z0;

    // Get the 8 neighboring voxel values
    auto v000 = intensityAt(x0, y0, z0);
    auto v001 = intensityAt(x0, y0, z1);
    auto v010 = intensityAt(x0, y1, z0);
    auto v011 = intensityAt(x0, y1, z1);
    auto v100 = intensityAt(x1, y0, z0);
    auto v101 = intensityAt(x1, y0, z1);
    auto v110 = intensityAt(x1, y1, z0);
    auto v111 = intensityAt(x1, y1, z1);

    // Trilinear interpolation
    double v00 = v000 * (1 - fx) + v100 * fx;
    double v01 = v001 * (1 - fx) + v101 * fx;
    double v10 = v010 * (1 - fx) + v110 * fx;
    double v11 = v011 * (1 - fx) + v111 * fx;

    double v0 = v00 * (1 - fy) + v10 * fy;
    double v1 = v01 * (1 - fy) + v11 * fy;

    double result = v0 * (1 - fz) + v1 * fz;

    return static_cast<std::uint16_t>(std::round(result));
}
