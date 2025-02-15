#include "vc/core/util/Slicing.hpp"
#include "vc/core/util/Surface.hpp"
#include "vc/core/types/ChunkedTensor.hpp"

#include "z5/factory.hxx"
#include <nlohmann/json.hpp>

#include <omp.h>

using shape = z5::types::ShapeType;
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

std::string time_str()
{
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    auto timer = system_clock::to_time_t(now);
    std::tm bt = *std::localtime(&timer);
    
    std::ostringstream oss;
    oss << std::put_time(&bt, "%Y%m%d%H%M%S");
    oss << std::setfill('0') << std::setw(3) << ms.count();

    return oss.str();
}

template <typename T, typename I>
float get_val(I &interp, cv::Vec3d l) {
    T v;
    interp.Evaluate(l[2], l[1], l[0], &v);
    return v;
}

bool check_existing_segments(const fs::path& tgt_dir, const cv::Vec3d& origin, 
                           const std::string& name_prefix, int search_effort) {
    for (const auto& entry : fs::directory_iterator(tgt_dir)) {
        if (!fs::is_directory(entry)) {
            continue;
        }

        std::string name = entry.path().filename();
        if (name.compare(0, name_prefix.size(), name_prefix)) {
            continue;
        }

        fs::path meta_fn = entry.path() / "meta.json";
        if (!fs::exists(meta_fn)) {
            continue;
        }

        std::ifstream meta_f(meta_fn);
        json meta = json::parse(meta_f);

        if (!meta.count("bbox") || meta.value("format","NONE") != "tifxyz") {
            continue;
        }

        SurfaceMeta other(entry.path(), meta);
        if (contains(other, origin, search_effort)) {
            std::cout << "Found overlapping segment at location: " << entry.path() << std::endl;
            return true;
        }
    }
    return false;
}

int main(int argc, char *argv[])
{
    if (argc != 7 && argc != 4) {
        std::cout << "usage: " << argv[0] << " <ome-zarr-volume> <tgt-dir> <json-params> <seed-x> <seed-y> <seed-z>" << std::endl;
        std::cout << "or:    " << argv[0] << " <ome-zarr-volume> <tgt-dir> <json-params>" << std::endl;
        return EXIT_SUCCESS;
    }

    fs::path vol_path = argv[1];
    fs::path tgt_dir = argv[2];
    const char *params_path = argv[3];

    std::ifstream params_f(params_path);
    json params = json::parse(params_f);

    z5::filesystem::handle::Group group(vol_path, z5::FileMode::FileMode::r);
    z5::filesystem::handle::Dataset ds_handle(group, "0", json::parse(std::ifstream(vol_path/"0/.zarray")).value<std::string>("dimension_separator","."));
    std::unique_ptr<z5::Dataset> ds = z5::filesystem::openDataset(ds_handle);

    std::cout << "zarr dataset size for scale group 0 " << ds->shape() << std::endl;
    std::cout << "chunk shape shape " << ds->chunking().blockShape() << std::endl;

    ChunkCache chunk_cache(params.value("cache_size", 1e9));

    passTroughComputor pass;
    Chunked3d<uint8_t,passTroughComputor> tensor(pass, ds.get(), &chunk_cache);
    CachedChunked3dInterpolator<uint8_t,passTroughComputor> interpolator(tensor);

    auto chunk_size = ds->chunking().blockShape();

    srand(clock());

    cv::Vec3d origin;

    std::string name_prefix = "auto_grown_";
    int tgt_overlap_count = params.value("tgt_overlap_count", 20);
    float min_area_cm = params.value("min_area_cm", 0.3);
    float step_size = params.value("step_size", 20);
    int search_effort = params.value("search_effort", 10);
    int generations = params.value("generations", 100);
    int thread_limit = params.value("thread_limit", 0);

    float voxelsize = json::parse(std::ifstream(vol_path/"meta.json"))["voxelsize"];
    
    std::filesystem::path cache_root = params["cache_root"];

    std::string mode = params.value("mode", "seed");
    
    std::cout << "mode: " << mode << std::endl;
    std::cout << "step size: " << step_size << std::endl;
    std::cout << "min_area_cm: " << min_area_cm << std::endl;
    std::cout << "tgt_overlap_count: " << tgt_overlap_count << std::endl;

    std::unordered_map<std::string,SurfaceMeta*> surfs;
    std::vector<SurfaceMeta*> surfs_v;
    SurfaceMeta *src;

    //expansion mode
    int count_overlap = 0;
    if (mode == "expansion") {
        //got trough all exising segments (that match filter/start with auto ...)
        //list which ones do not yet less N overlapping (in symlink dir)
        //shuffle
        //iterate and for every one
            //select a random point (close to edge?)
            //check against list if other surf in bbox if we can find the point
            //if yes add symlinkg between the two segs
            //if both still have less than N then grow a seg from the seed
            //after growing, check locations on the new seg agains all existing segs

        for (const auto& entry : fs::directory_iterator(tgt_dir))
            if (fs::is_directory(entry)) {
                std::string name = entry.path().filename();
                if (name.compare(0, name_prefix.size(), name_prefix))
                    continue;

                std::cout << entry.path() << entry.path().filename() << std::endl;

                fs::path meta_fn = entry.path() / "meta.json";
                if (!fs::exists(meta_fn))
                    continue;

                std::ifstream meta_f(meta_fn);
                json meta = json::parse(meta_f);

                if (!meta.count("bbox"))
                    continue;

                if (meta.value("format","NONE") != "tifxyz")
                    continue;

                SurfaceMeta *sm = new SurfaceMeta(entry.path(), meta);
                sm->readOverlapping();

                surfs[name] = sm;
                surfs_v.push_back(sm);
            }
            
        if (!surfs.size()) {
            std::cerr << "ERROR: no seed surfaces found in expansion mode" << std::endl; 
            return EXIT_FAILURE;
        }
        
        std::default_random_engine rng(clock());
        std::shuffle(std::begin(surfs_v), std::end(surfs_v), rng);


        for(auto &it : surfs_v) {
            src = it;
            cv::Mat_<cv::Vec3f> points = src->surf()->rawPoints();
            int w = points.cols;
            int h = points.rows;

            bool found = false;
            for (int r=0;r<10;r++) {
                if ((rand() % 2) == 0)
                {
                    cv::Vec2i p = {rand() % h, rand() % w};
                    
                    if (points(p)[0] != -1 && get_val<double,CachedChunked3dInterpolator<uint8_t,passTroughComputor>>(interpolator, points(p)) >= 128) {
                        found = true;
                        origin = points(p);
                        break;
                    }
                }
                else {
                    cv::Vec2f p;
                    int side = rand() % 4;
                    if (side == 0)
                        p = {rand() % h, 0};
                    else if (side == 1)
                        p = {0, rand() % w};
                    else if (side == 2)
                        p = {rand() % h, w-1};
                    else if (side == 3)
                        p = {h-1, rand() % w};

                    cv::Vec2f searchdir = cv::Vec2f(h/2,w/2) - p;
                    cv::normalize(searchdir, searchdir);
                    found = false;
                    for(int i=0;i<std::min(w/2/abs(searchdir[1]),h/2/abs(searchdir[0]));i++,p+=searchdir) {
                        found = true;
                        cv::Vec2i p_eval = p;
                        for(int r=0;r<5;r++) {
                            cv::Vec2i p_eval = p+r*searchdir;
                            if (points(p_eval)[0] == -1 ||get_val<double,CachedChunked3dInterpolator<uint8_t,passTroughComputor>>(interpolator, points(p_eval)) < 128) {
                                found = false;
                                break;
                            }
                        }
                        if (found) {
                            cv::Vec2i p_eval = p+2*searchdir;
                            origin = points(p_eval);
                            break;
                        }
                    }
                }
            }

            if (!found)
                continue;

            count_overlap = 0;
            for(auto comp : surfs_v) {
                if (comp == src)
                    continue;
                if (contains(*comp, origin, search_effort))
                    count_overlap++;
                if (count_overlap >= tgt_overlap_count-1)
                    break;
            }
            if (count_overlap < tgt_overlap_count-1)
                break;
        }

        std::cout << "found potential overlapping starting seed" << origin << "with overlap " << count_overlap << std::endl;
    }
    else {
        if (argc == 7) {
            mode = "explicit_seed";
            origin = {atof(argv[4]), atof(argv[5]), atof(argv[6])};
            double v;
            interpolator.Evaluate(origin[2], origin[1], origin[0], &v);
            std::cout << "seed location " << origin << " value is " << v << std::endl;
        }
        else {
            mode = "random_seed";
            int count = 0;
            bool succ = false;
            int max_attempts = 1000;
            
            while(count < max_attempts && !succ) {
                origin = {128 + (rand() % (ds->shape(2)-384)), 
                         128 + (rand() % (ds->shape(1)-384)), 
                         128 + (rand() % (ds->shape(0)-384))};

                count++;
                auto chunk_id = chunk_size;
                chunk_id[0] = origin[2]/chunk_id[0];
                chunk_id[1] = origin[1]/chunk_id[1];
                chunk_id[2] = origin[0]/chunk_id[2];

                if (!ds->chunkExists(chunk_id))
                    continue;

                cv::Vec3d dir = {(rand() % 1024) - 512,
                                (rand() % 1024) - 512,
                                (rand() % 1024) - 512};
                cv::normalize(dir, dir);

                for(int i=0;i<128;i++) {
                    double v;
                    cv::Vec3d p = origin + i*dir;
                    interpolator.Evaluate(p[2], p[1], p[0], &v);
                    if (v >= 128) {
                        if (check_existing_segments(tgt_dir, p, name_prefix, search_effort))
                            continue;
                        succ = true;
                        origin = p;
                        std::cout << "Found seed location " << origin << " value: " << v << std::endl;
                        break;
                    }
                }
            }

            if (!succ) {
                std::cout << "ERROR: Could not find valid non-overlapping seed location after " 
                        << max_attempts << " attempts" << std::endl;
                return EXIT_SUCCESS;
            }
        }
    }

    if (thread_limit)
        omp_set_num_threads(thread_limit);

    QuadSurface *surf = space_tracing_quad_phys(ds.get(), 1.0, &chunk_cache, origin, generations, step_size, cache_root);

    double area_cm2 = (*surf->meta)["area_vx2"].get<double>()*voxelsize*voxelsize/1e8;
    if (area_cm2 < min_area_cm)
        return EXIT_SUCCESS;

    (*surf->meta)["area_cm2"] = area_cm2;
    (*surf->meta)["source"] = "vc_grow_seg_from_seed";
    (*surf->meta)["vc_gsfs_params"] = params;
    (*surf->meta)["vc_gsfs_mode"] = mode;
    (*surf->meta)["vc_gsfs_version"] = "dev";
    if (mode == "expansion")
        (*surf->meta)["seed_overlap"] = count_overlap;
    std::string uuid = name_prefix + time_str();
    fs::path seg_dir = tgt_dir / uuid;
    std::cout << "saving " << seg_dir << std::endl;
    surf->save(seg_dir, uuid);

    SurfaceMeta current;

    if (mode == "expansion") {
        current.path = seg_dir;
        current.setSurf(surf);
        current.bbox = surf->bbox();

        fs::path overlap_dir = current.path / "overlapping";
        fs::create_directory(overlap_dir);

        {std::ofstream touch(overlap_dir/src->name());}

        fs::path overlap_src = src->path / "overlapping";
        fs::create_directory(overlap_src);
        {std::ofstream touch(overlap_src/current.name());}

        for(auto &s : surfs_v)
            if (overlap(current, *s, search_effort)) {
                std::ofstream touch_me(overlap_dir/s->name());
                fs::path overlap_other = s->path / "overlapping";
                fs::create_directory(overlap_other);
                std::ofstream touch_you(overlap_other/current.name());
            }

        for (const auto& entry : fs::directory_iterator(tgt_dir))
            if (fs::is_directory(entry) && !surfs.count(entry.path().filename()))
            {
                std::string name = entry.path().filename();
                if (name.compare(0, name_prefix.size(), name_prefix))
                    continue;

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

                if (overlap(current, other, search_effort)) {
                    std::ofstream touch_me(overlap_dir/other.name());
                    fs::path overlap_other = other.path / "overlapping";
                    fs::create_directory(overlap_other);
                    std::ofstream touch_you(overlap_other/current.name());
                }
            }
    }
    
    return EXIT_SUCCESS;
}
