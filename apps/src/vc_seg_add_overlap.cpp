#include <nlohmann/json.hpp>
#include "vc/Slicing.hpp"
#include "vc/Surface.hpp"

#include <fstream>



using json = nlohmann::json;


int main(int argc, char *argv[])
{
    if (argc != 3) {
        std::cout << "usage: " << argv[0] << " <tgt-dir> <single-tiffxyz>" << std::endl;
        std::cout << "   this will check for overlap between any tiffxyz in target dir and <single-tiffxyz> and add overlap metadata" << std::endl;
        return EXIT_SUCCESS;
    }
    std::filesystem::path tgt_dir = argv[1];
    std::filesystem::path seg_dir = argv[2];

    int search_iters = 10;

    srand(clock());

    SurfaceMeta current(seg_dir);

    std::filesystem::path overlap_dir = current.path / "overlapping";
    std::filesystem::create_directory(overlap_dir);

    for (const auto& entry : std::filesystem::directory_iterator(tgt_dir))
        if (std::filesystem::is_directory(entry))
        {
            std::string name = entry.path().filename();

            if (name == current.name())
                continue;

            std::filesystem::path meta_fn = entry.path() / "meta.json";
            if (!std::filesystem::exists(meta_fn))
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
                std::filesystem::path overlap_other = other.path / "overlapping";
                std::filesystem::create_directory(overlap_other);
                std::ofstream touch_you(overlap_other/current.name());
            }
        }
    
    return EXIT_SUCCESS;
}
