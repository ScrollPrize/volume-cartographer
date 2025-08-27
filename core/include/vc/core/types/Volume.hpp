#pragma once

/** @file */

#include <cstddef>
#include <cstdint>
#include <mutex>

#include <filesystem>
#include "vc/core/types/DiskBasedObjectBaseClass.hpp"

#include "z5/types/types.hxx"

namespace z5
{
    class Dataset;

    namespace filesystem
    {
        namespace handle
        {
            class File;
        }
    }
}


namespace volcart
{

    
/**
 * @class Volume
 * @author Sean Karlage
 *
 * @brief Volumetric image data
 *
 * Provides access to a volumetric dataset, such as a CT scan. By default,
 * slices are cached in memory using volcart::LRUCache.
 *
 * @ingroup Types
 */
// shared_from_this used in Python bindings
// https://en.cppreference.com/w/cpp/memory/enable_shared_from_this
class Volume : public DiskBasedObjectBaseClass,
               public std::enable_shared_from_this<Volume>
{
public:

    /** Shared pointer type */
    using Pointer = std::shared_ptr<Volume>;

    /** Default slice cache capacity */
    static constexpr std::size_t DEFAULT_CAPACITY = 200;

    /**@{*/
    /** Default constructor. Cannot be constructed without path. */
    Volume() = delete;

    /** @brief Load the Volume from a directory path */
    explicit Volume(std::filesystem::path path);

    /** @brief Make a new Volume at the specified path */
    Volume(std::filesystem::path path, Identifier uuid, std::string name);

    /** @overload Volume(std::filesystem::path) */
    static Pointer New(std::filesystem::path path);

    /** @overload Volume(std::filesystem::path, Identifier, std::string) */
    static Pointer New(
        std::filesystem::path path, Identifier uuid, std::string name);
    /**@}*/
    
    /** is ZARR volume **/
    bool isZarr{false};

    /**@{*/
    /** @brief Get the slice width */
    int sliceWidth() const;
    /** @brief Get the slice height */
    int sliceHeight() const;
    /** @brief Get the number of slices */
    int numSlices() const;
    /** @brief Get the voxel size (in microns) */
    double voxelSize() const;
    /** @brief Get the minimum intensity value in the Volume */
    double min() const;
    /** @brief Get the maximum intensity value in the Volume */
    double max() const;
    /**@}*/

    /**@{*/
    /** @brief Set the expected width of the slice images */
    void setSliceWidth(int w);
    /** @brief Set the expected height of the slice images */
    void setSliceHeight(int h);
    /** @brief Set the expected number of slice images */
    void setNumberOfSlices(std::size_t numSlices);
    /** @brief Set the voxel size (in microns) */
    void setVoxelSize(double s);
    /** @brief Set the minimum value in the Volume */
    void setMin(double m);
    /** @brief Set the maximum value in the Volume */
    void setMax(double m);
    /**@}*/

    z5::Dataset *zarrDataset(int level = 0);
    size_t numScales();
    
protected:
    /** Slice width */
    int width_{0};
    /** Slice height */
    int height_{0};
    /** NnumSliceCharacters_umber of slices */
    int slices_{0};

    std::unique_ptr<z5::filesystem::handle::File> zarrFile_;
    std::vector<std::unique_ptr<z5::Dataset>> zarrDs_;
    nlohmann::json zarrGroup_;
    void zarrOpen();
};
}  // namespace volcart
