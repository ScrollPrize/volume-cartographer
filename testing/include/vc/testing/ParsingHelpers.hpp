#pragma once

/**
 * @file
 *
 * Provides helpful parsing methods that are used by multiple testing files
 * when reading in mesh or point cloud data
 */

#include <filesystem>
#include "vc/core/types/SimpleMesh.hpp"

namespace volcart::testing
{

class ParsingHelpers
{

public:
    static void ParsePLYFile(
        const std::filesystem::path& filepath,
        std::vector<SimpleMesh::Vertex>& verts,
        std::vector<SimpleMesh::Cell>& faces);
    static void ParseOBJFile(
        const std::filesystem::path& filepath,
        std::vector<SimpleMesh::Vertex>& points,
        std::vector<SimpleMesh::Cell>& cells);
};
}  // namespace volcart::testing
