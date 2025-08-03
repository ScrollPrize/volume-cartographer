#include "vc/core/util/surface_metrics.hpp"
#include "vc/core/util/VCCollection.hpp"
#include "vc/core/util/Surface.hpp"
#include <opencv2/imgcodecs.hpp>
#include <boost/program_options.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>

namespace po = boost::program_options;

int main(int argc, char** argv)
{
    po::options_description desc("Calculate surface metrics based on a point collection.");
    desc.add_options()
        ("help,h", "Print help")
        ("collection", po::value<std::string>(), "Input point collection file (.json)")
        ("surface", po::value<std::string>(), "Input surface file (.tif)")
        ("winding", po::value<std::string>(), "Input winding file (.tif)")
        ("output", po::value<std::string>(), "Output metrics file (.json)");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 0;
    }

    if (!vm.count("collection") || !vm.count("surface") || !vm.count("output") || !vm.count("winding")) {
        std::cerr << "Error: --collection, --surface, --winding, and --output are required." << std::endl;
        return 1;
    }

    std::string collection_path = vm["collection"].as<std::string>();
    std::string surface_path = vm["surface"].as<std::string>();
    std::string winding_path = vm["winding"].as<std::string>();
    std::string output_path = vm["output"].as<std::string>();

    ChaoVis::VCCollection collection;
    if (!collection.loadFromJSON(collection_path)) {
        std::cerr << "Error: Failed to load point collection from " << collection_path << std::endl;
        return 1;
    }

    QuadSurface* surface = load_quad_from_tifxyz(surface_path);
    if (!surface) {
        std::cerr << "Error: Failed to load surface from " << surface_path << std::endl;
        return 1;
    }

    cv::Mat_<float> winding = cv::imread(winding_path, cv::IMREAD_UNCHANGED);
    if (winding.empty()) {
        std::cerr << "Error: Failed to load winding from " << winding_path << std::endl;
        return 1;
    }

    nlohmann::json metrics = vc::apps::calc_point_metrics(collection, surface, winding);

    delete surface;

    std::ofstream o(output_path);
    if (!o.is_open()) {
        std::cerr << "Error: Failed to open output file " << output_path << std::endl;
        return 1;
    }

    o << metrics.dump(4);
    o.close();

    std::cout << "Successfully calculated metrics and saved to " << output_path << std::endl;

    return 0;
}