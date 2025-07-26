#include <cstddef>

#include <boost/program_options.hpp>

#include "vc/Logging.hpp"
#include "vc/app_support/ProgressIndicator.hpp"
#include <filesystem>#include "vc/core/io/MeshIO.hpp"
#include "vc/core/types/PerPixelMap.hpp"
#include "vc/meshing/OrientNormals.hpp"
#include "vc/texturing/AngleBasedFlattening.hpp"
#include "vc/texturing/PPMGenerator.hpp"

namespace vcm = vc::meshing;
namespace vct = vc::texturing;
namespace boost::program_options = boost::program_options;

auto main(int argc, char* argv[]) -> int
{
    // clang-format off
    boost::program_options::options_description all("Usage");
    all.add_options()
        ("help,h", "Show this message")
        ("input-mesh,i", boost::program_options::value<std::string>()->required(),
            "Path to the input mesh")
        ("output-ppm,o", boost::program_options::value<std::string>()->required(),
            "Path for the output ppm")
        ("uv-reuse", "If input-mesh is specified, attempt to use its existing "
            "UV map instead of generating a new one.")
        ("orient-normals", "Auto-orient surface normals towards the mesh centroid");
    // clang-format on

    // parsed will hold the values of all parsed options as a Map
    boost::program_options::variables_map parsed;
    boost::program_options::store(boost::program_options::command_line_parser(argc, argv).options(all).run(), parsed);

    // Show the help message
    if (parsed.count("help") > 0 || argc < 3) {
        std::cout << all << '\n';
        return EXIT_SUCCESS;
    }

    // Warn of missing options
    try {
        po::notify(parsed);
    } catch (po::error& e) {
        vc::Logger()->error(e.what());
        return EXIT_FAILURE;
    }

    // Get inputs
    std::filesystem::path meshPath = parsed["input-mesh"].as<std::string>();
    std::filesystem::path ppmPath = parsed["output-ppm"].as<std::string>();

    // Load mesh
    vc::Logger()->info("Loading mesh");
    auto meshFile = vc::ReadMesh(meshPath);
    auto mesh = meshFile.mesh;
    auto uvMap = meshFile.uv;

    // Reorient the surface normals
    if (parsed.count("orient-normals") > 0) {
        vcm::OrientNormals orient;
        orient.setReferenceMode(vcm::OrientNormals::ReferenceMode::Centroid);
        orient.setMesh(mesh);
        mesh = orient.compute();
    }

    // Generate UV map
    auto genUV = parsed.count("uv-reuse") == 0;
    if (genUV or not uvMap) {
        // ABF
        vct::AngleBasedFlattening abf;
        abf.setMesh(mesh);
        abf.compute();

        // Get UV map
        uvMap = abf.getUVMap();
    }

    auto width = static_cast<std::size_t>(std::ceil(uvMap->ratio().width));
    auto height =
        static_cast<std::size_t>(std::ceil(width / uvMap->ratio().aspect));

    // PPM
    vc::Logger()->info("Generating per-pixel map");
    vct::PPMGenerator p;
    p.setDimensions(height, width);
    p.setMesh(mesh);
    p.setUVMap(uvMap);
    p.compute();

    // Write PPM
    vc::Logger()->info("Writing per-pixel map");
    vc::PerPixelMap::WritePPM(ppmPath, *p.getPPM());

    return EXIT_SUCCESS;
}
