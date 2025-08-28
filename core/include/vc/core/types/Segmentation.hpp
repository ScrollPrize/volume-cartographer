#pragma once

/** @file */

#include <filesystem>
#include "vc/core/types/DiskBasedObjectBaseClass.hpp"
#include "vc/core/types/OrderedPointSet.hpp"
#include "vc/core/types/Volume.hpp"

#include <variant>

class Segmentation : public DiskBasedObjectBaseClass
{
public:
    explicit Segmentation(std::filesystem::path path);
    Segmentation(std::filesystem::path path, std::string uuid, std::string name);
    static std::shared_ptr<Segmentation> New(std::filesystem::path path);
    static std::shared_ptr<Segmentation> New(std::filesystem::path path, std::string uuid, std::string name);

};

