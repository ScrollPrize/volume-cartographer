#include "vc/core/util/Slicing.hpp"
#include "vc/core/util/Surface.hpp"
#include <nlohmann/json.hpp>

#include <fstream>

namespace fs = std::filesystem;

using json = nlohmann::json;

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
    if (argc != 3) {
        std::cout << "usage: " << argv[0] << " <tgt-dir> <single-tiffxyz>" << std::endl;
        std::cout << "   this will check for overlap between any tiffxyz in target dir and <single-tiffxyz> and add overlap metadata" << std::endl;
        return EXIT_SUCCESS;
    }
    fs::path tgt_dir = argv[1];
    fs::path seg_dir = argv[2];

    int search_iters = 10;

    srand(clock());

    SurfaceMeta current(seg_dir);

    fs::path overlap_dir = current.path / "overlapping";
    fs::create_directory(overlap_dir);

    for (const auto& entry : fs::directory_iterator(tgt_dir))
        if (fs::is_directory(entry))
        {
            std::string name = entry.path().filename();

            if (name == current.name())
                continue;

            fs::path meta_fn = entry.path() / "meta.json";
            if (!fs::exists(meta_fn))
                continue;

            std::ifstream meta_f(meta_fn);
            json meta = json::parse(meta_f);

            if (!meta.count("bbox"))
                continue;

            if (meta.value("format","NONE") != "tifxyz")
                continue;

            SurfaceMeta other = SurfaceMeta(entry.path(), meta);
            other.readOverlapping();

            if (overlap(current, other, search_iters)) {
                std::ofstream touch_me(overlap_dir/other.name());
                fs::path overlap_other = other.path / "overlapping";
                fs::create_directory(overlap_other);
                std::ofstream touch_you(overlap_other/current.name());
            }
        }
    
    return EXIT_SUCCESS;
}
