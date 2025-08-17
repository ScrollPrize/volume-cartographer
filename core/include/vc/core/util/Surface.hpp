#pragma once
#include <filesystem>
#include <set>
#include <vector>
#include <string>

#include <opencv2/core.hpp>
#include <nlohmann/json.hpp>

#include "SurfaceDef.hpp"

#define Z_DBG_GEN_PREFIX "auto_grown_"

class QuadSurface;
class ChunkCache;

namespace z5 {
    class Dataset;
}

class SurfacePointer
{
public:
    virtual SurfacePointer *clone() const = 0;
};

class TrivialSurfacePointer : public SurfacePointer
{
public:
    TrivialSurfacePointer(cv::Vec3f loc_) : loc(loc_) {}
    SurfacePointer *clone() const override { return new TrivialSurfacePointer(*this); }
    cv::Vec3f loc;
};

struct Rect3D {
    cv::Vec3f low = {0,0,0};
    cv::Vec3f high = {0,0,0};
};



struct json_metadata {
    float area_vx2 = 0.0f;
    float area_cm2 = 0.0f;
    double max_cost = 0.0;
    double avg_cost = 0.0;
    int max_gen = 0;
    std::vector<float> gen_avg_cost;
    std::vector<float> gen_max_cost;
    cv::Vec3f seed = {0, 0, 0};
    double elapsed_time_s = 0.0;

    struct Tags {
        bool approved = false;
        bool defective = false;
        bool reviewed = false;
        bool revisit = false;
    } tags;

    struct TagInfo {
        std::string user;
        std::string date;
        nlohmann::json extra;  // For any additional fields like "source" for partial_review
    };

    std::map<std::string, TagInfo> tag_metadata;
    nlohmann::json custom_fields;

    json_metadata() = default;
    json_metadata(float area_vx2_,
                  float area_cm2_,
                  double max_cost_,
                  double avg_cost_,
                  int max_gen_,
                  const std::vector<float>& gen_avg_cost_,
                  const std::vector<float>& gen_max_cost_,
                  const cv::Vec3f& seed_,
                  double elapsed_time_s_)
        : area_vx2(area_vx2_),
          area_cm2(area_cm2_),
          max_cost(max_cost_),
          avg_cost(avg_cost_),
          max_gen(max_gen_),
          gen_avg_cost(gen_avg_cost_),
          gen_max_cost(gen_max_cost_),
          seed(seed_),
          elapsed_time_s(elapsed_time_s_) {}

=    json_metadata(float area_vx2_,
                  float area_cm2_,
                  double max_cost_,
                  double avg_cost_,
                  int max_gen_,
                  const std::vector<float>& gen_avg_cost_,
                  const std::vector<float>& gen_max_cost_,
                  const cv::Vec3f& seed_,
                  double elapsed_time_s_,
                  bool approved_,
                  bool defective_,
                  bool reviewed_,
                  bool revisit_)
        : area_vx2(area_vx2_),
          area_cm2(area_cm2_),
          max_cost(max_cost_),
          avg_cost(avg_cost_),
          max_gen(max_gen_),
          gen_avg_cost(gen_avg_cost_),
          gen_max_cost(gen_max_cost_),
          seed(seed_),
          elapsed_time_s(elapsed_time_s_) {
        tags.approved = approved_;
        tags.defective = defective_;
        tags.reviewed = reviewed_;
        tags.revisit = revisit_;
    }

    void from_json(const nlohmann::json& j) {
        area_vx2 = j.value("area_vx2", 0.0f);
        area_cm2 = j.value("area_cm2", 0.0f);
        max_cost = j.value("max_cost", 0.0);
        avg_cost = j.value("avg_cost", 0.0);
        max_gen = j.value("max_gen", 0);
        elapsed_time_s = j.value("elapsed_time_s", 0.0);

        gen_avg_cost.clear();
        if (j.contains("gen_avg_cost") && j["gen_avg_cost"].is_array()) {
            for (const auto& val : j["gen_avg_cost"]) {
                gen_avg_cost.push_back(val.get<float>());
            }
        }

        gen_max_cost.clear();
        if (j.contains("gen_max_cost") && j["gen_max_cost"].is_array()) {
            for (const auto& val : j["gen_max_cost"]) {
                gen_max_cost.push_back(val.get<float>());
            }
        }

        seed = {0, 0, 0};
        if (j.contains("seed") && j["seed"].is_array() && j["seed"].size() == 3) {
            seed[0] = j["seed"][0].get<float>();
            seed[1] = j["seed"][1].get<float>();
            seed[2] = j["seed"][2].get<float>();
        }

        tags.approved = false;
        tags.defective = false;
        tags.reviewed = false;
        tags.revisit = false;
        tag_metadata.clear();

        if (j.contains("tags")) {
            const auto& tags_json = j["tags"];

            auto parse_tag = [this](const nlohmann::json& tag_json, const std::string& tag_name, bool& tag_flag) {
                if (tag_json.contains(tag_name)) {
                    tag_flag = true;
                    if (tag_json[tag_name].is_object()) {
                        TagInfo info;
                        if (tag_json[tag_name].contains("user")) {
                            info.user = tag_json[tag_name]["user"].get<std::string>();
                        }
                        if (tag_json[tag_name].contains("date")) {
                            info.date = tag_json[tag_name]["date"].get<std::string>();
                        }
                        // Store any extra fields
                        for (auto& [key, value] : tag_json[tag_name].items()) {
                            if (key != "user" && key != "date") {
                                info.extra[key] = value;
                            }
                        }
                        tag_metadata[tag_name] = info;
                    }
                }
            };

            parse_tag(tags_json, "approved", tags.approved);
            parse_tag(tags_json, "defective", tags.defective);
            parse_tag(tags_json, "reviewed", tags.reviewed);
            parse_tag(tags_json, "revisit", tags.revisit);

            if (tags_json.contains("partial_review")) {
                TagInfo info;
                if (tags_json["partial_review"].is_object()) {
                    if (tags_json["partial_review"].contains("user")) {
                        info.user = tags_json["partial_review"]["user"].get<std::string>();
                    }
                    if (tags_json["partial_review"].contains("date")) {
                        info.date = tags_json["partial_review"]["date"].get<std::string>();
                    }
                    if (tags_json["partial_review"].contains("source")) {
                        info.extra["source"] = tags_json["partial_review"]["source"];
                    }
                }
                tag_metadata["partial_review"] = info;
            }
        }

        for (auto& [key, value] : j.items()) {
            if (key != "area_vx2" && key != "area_cm2" && key != "max_cost" &&
                key != "avg_cost" && key != "max_gen" && key != "elapsed_time_s" &&
                key != "gen_avg_cost" && key != "gen_max_cost" && key != "seed" &&
                key != "tags") {
                custom_fields[key] = value;
            }
        }
    }

    nlohmann::json to_json() const {
        nlohmann::json j;
        j["area_vx2"] = area_vx2;
        j["area_cm2"] = area_cm2;
        j["max_cost"] = max_cost;
        j["avg_cost"] = avg_cost;
        j["max_gen"] = max_gen;
        j["elapsed_time_s"] = elapsed_time_s;
        j["gen_avg_cost"] = gen_avg_cost;
        j["gen_max_cost"] = gen_max_cost;
        j["seed"] = nlohmann::json::array({seed[0], seed[1], seed[2]});

        nlohmann::json tags_json;

        auto write_tag = [this, &tags_json](const std::string& tag_name, bool tag_value) {
            if (tag_value) {
                if (tag_metadata.count(tag_name)) {
                    nlohmann::json tag_obj;
                    const auto& info = tag_metadata.at(tag_name);
                    if (!info.user.empty()) tag_obj["user"] = info.user;
                    if (!info.date.empty()) tag_obj["date"] = info.date;
                    for (auto& [key, value] : info.extra.items()) {
                        tag_obj[key] = value;
                    }
                    tags_json[tag_name] = tag_obj;
                } else {
                    tags_json[tag_name] = true;
                }
            }
        };

        write_tag("approved", tags.approved);
        write_tag("defective", tags.defective);
        write_tag("reviewed", tags.reviewed);
        write_tag("revisit", tags.revisit);

        if (tag_metadata.count("partial_review")) {
            const auto& info = tag_metadata.at("partial_review");
            nlohmann::json pr_obj;
            if (!info.user.empty()) pr_obj["user"] = info.user;
            if (!info.date.empty()) pr_obj["date"] = info.date;
            for (auto& [key, value] : info.extra.items()) {
                pr_obj[key] = value;
            }
            tags_json["partial_review"] = pr_obj;
        }

        j["tags"] = tags_json;

        for (auto& [key, value] : custom_fields.items()) {
            j[key] = value;
        }

        return j;
    }

    bool isApproved() const { return tags.approved; }
    bool isDefective() const { return tags.defective; }
    bool isReviewed() const { return tags.reviewed; }
    bool isRevisit() const { return tags.revisit; }

    bool hasPartialReview() const { return tag_metadata.count("partial_review") > 0; }

    void setApproved(bool value = true, const std::string& user = "", const std::string& date = "") {
        tags.approved = value;
        if (value && (!user.empty() || !date.empty())) {
            tag_metadata["approved"] = {user, date, {}};
        } else if (!value) {
            tag_metadata.erase("approved");
        }
    }

    void setDefective(bool value = true, const std::string& user = "", const std::string& date = "") {
        tags.defective = value;
        if (value && (!user.empty() || !date.empty())) {
            tag_metadata["defective"] = {user, date, {}};
        } else if (!value) {
            tag_metadata.erase("defective");
        }
    }

    void setReviewed(bool value = true, const std::string& user = "", const std::string& date = "") {
        tags.reviewed = value;
        if (value && (!user.empty() || !date.empty())) {
            tag_metadata["reviewed"] = {user, date, {}};
        } else if (!value) {
            tag_metadata.erase("reviewed");
        }
    }

    void setRevisit(bool value = true, const std::string& user = "", const std::string& date = "") {
        tags.revisit = value;
        if (value && (!user.empty() || !date.empty())) {
            tag_metadata["revisit"] = {user, date, {}};
        } else if (!value) {
            tag_metadata.erase("revisit");
        }
    }

    void setPartialReview(const std::string& source, const std::string& user = "", const std::string& date = "") {
        TagInfo info;
        info.user = user;
        info.date = date;
        info.extra["source"] = source;
        tag_metadata["partial_review"] = info;
    }

    void clearPartialReview() {
        tag_metadata.erase("partial_review");
    }
};

bool intersect(const Rect3D &a, const Rect3D &b);
Rect3D expand_rect(const Rect3D &a, const cv::Vec3f &p);

QuadSurface *load_quad_from_vcps(const std::string &path);
QuadSurface *load_quad_from_obj(const std::string &path);
QuadSurface *load_quad_from_tifxyz(const std::string &path);
QuadSurface *space_tracing_quad_phys(z5::Dataset *ds, float scale, ChunkCache *cache, cv::Vec3f origin, int generations = 100, float step = 10, const std::string &cache_root = "", float voxelsize = 1.0);
QuadSurface *regularized_local_quad(QuadSurface *src, SurfacePointer *ptr, int w, int h, int step_search = 100, int step_out = 5);
QuadSurface *smooth_vc_segmentation(QuadSurface *src);

cv::Vec3f vx_from_orig_norm(const cv::Vec3f &o, const cv::Vec3f &n);
cv::Vec3f vy_from_orig_norm(const cv::Vec3f &o, const cv::Vec3f &n);

//base surface class
class Surface
{
public:
    virtual ~Surface();

    // a pointer in some central location
    virtual SurfacePointer *pointer() = 0;
    
    //move pointer within internal coordinate system
    virtual void move(SurfacePointer *ptr, const cv::Vec3f &offset) = 0;
    //does the pointer location contain valid surface data
    virtual bool valid(SurfacePointer *ptr, const cv::Vec3f &offset = {0,0,0}) = 0;
    //nominal pointer coordinates (in "output" coordinates)
    virtual cv::Vec3f loc(SurfacePointer *ptr, const cv::Vec3f &offset = {0,0,0}) = 0;
    //read coord at pointer location, potentially with (3) offset
    virtual cv::Vec3f coord(SurfacePointer *ptr, const cv::Vec3f &offset = {0,0,0}) = 0;
    virtual cv::Vec3f normal(SurfacePointer *ptr, const cv::Vec3f &offset = {0,0,0}) = 0;
    virtual float pointTo(SurfacePointer *ptr, const cv::Vec3f &coord, float th, int max_iters = 1000) = 0;
    //coordgenerator relative to ptr&offset
    //needs to be deleted after use
    virtual void gen(cv::Mat_<cv::Vec3f> *coords, cv::Mat_<cv::Vec3f> *normals, cv::Size size, SurfacePointer *ptr, float scale, const cv::Vec3f &offset) = 0;

    json_metadata metadata;
    std::filesystem::path path;
    SurfaceID id;
};

class PlaneSurface : public Surface
{
public:
    //Surface API FIXME
    TrivialSurfacePointer *pointer() override;
    void move(SurfacePointer *ptr, const cv::Vec3f &offset);
    bool valid(SurfacePointer *ptr, const cv::Vec3f &offset = {0,0,0}) override { return true; };
    cv::Vec3f loc(SurfacePointer *ptr, const cv::Vec3f &offset = {0,0,0}) override;
    cv::Vec3f coord(SurfacePointer *ptr, const cv::Vec3f &offset = {0,0,0}) override;
    cv::Vec3f normal(SurfacePointer *ptr, const cv::Vec3f &offset = {0,0,0}) override;
    float pointTo(SurfacePointer *ptr, const cv::Vec3f &coord, float th, int max_iters = 1000) override { abort(); };

    PlaneSurface() {};
    PlaneSurface(cv::Vec3f origin_, cv::Vec3f normal_);

    void gen(cv::Mat_<cv::Vec3f> *coords, cv::Mat_<cv::Vec3f> *normals, cv::Size size, SurfacePointer *ptr, float scale, const cv::Vec3f &offset) override;

    float pointDist(cv::Vec3f wp);
    cv::Vec3f project(cv::Vec3f wp, float render_scale = 1.0, float coord_scale = 1.0);
    void setNormal(cv::Vec3f normal);
    void setOrigin(cv::Vec3f origin);
    cv::Vec3f origin();
    float scalarp(cv::Vec3f point) const;
protected:
    void update();
    cv::Vec3f _normal = {0,0,1};
    cv::Vec3f _origin = {0,0,0};
    cv::Matx33d _M;
    cv::Vec3d _T;
};

//quads based surface class with a pointer implementing a nominal scale of 1 voxel
class QuadSurface : public Surface
{
public:
    TrivialSurfacePointer *pointer();
    QuadSurface() {};
    // points will be cloned in constructor
    QuadSurface(const cv::Mat_<cv::Vec3f> &points, const cv::Vec2f &scale);
    // points will not be cloned in constructor, but pointer stored
    QuadSurface(cv::Mat_<cv::Vec3f> *points, const cv::Vec2f &scale);
    ~QuadSurface();
    void move(SurfacePointer *ptr, const cv::Vec3f &offset) override;
    bool valid(SurfacePointer *ptr, const cv::Vec3f &offset = {0,0,0}) override;
    cv::Vec3f loc(SurfacePointer *ptr, const cv::Vec3f &offset = {0,0,0}) override;
    cv::Vec3f loc_raw(SurfacePointer *ptr);
    cv::Vec3f coord(SurfacePointer *ptr, const cv::Vec3f &offset = {0,0,0}) override;
    cv::Vec3f normal(SurfacePointer *ptr, const cv::Vec3f &offset = {0,0,0}) override;
    void gen(cv::Mat_<cv::Vec3f> *coords, cv::Mat_<cv::Vec3f> *normals, cv::Size size, SurfacePointer *ptr, float scale, const cv::Vec3f &offset) override;
    float pointTo(SurfacePointer *ptr, const cv::Vec3f &tgt, float th, int max_iters = 1000) override;
    cv::Size size();
    cv::Vec2f scale() const;

    void save(const std::string &path, const std::string &uuid);
    void save(std::filesystem::path &path);
    Rect3D bbox();

    virtual cv::Mat_<cv::Vec3f> rawPoints() { return *_points; }
    virtual cv::Mat_<cv::Vec3f> *rawPointsPtr() { return _points; }

    friend QuadSurface *regularized_local_quad(QuadSurface *src, SurfacePointer *ptr, int w, int h, int step_search, int step_out);
    friend QuadSurface *smooth_vc_segmentation(QuadSurface *src);
    friend class ControlPointSurface;
    cv::Vec2f _scale;
protected:
    cv::Mat_<cv::Vec3f>* _points = nullptr;
    cv::Rect _bounds;
    cv::Vec3f _center;
    Rect3D _bbox = {{-1,-1,-1},{-1,-1,-1}};
};


//surface representing some operation on top of a base surface
//by default all ops but gen() are forwarded to the base
class DeltaSurface : public Surface
{
public:
    //default - just assign base ptr, override if additional processing necessary
    //like relocate ctrl points, mark as dirty, ...
    virtual void setBase(Surface *base);
    DeltaSurface(Surface *base);
    
    virtual SurfacePointer *pointer() override;
    
    void move(SurfacePointer *ptr, const cv::Vec3f &offset) override;
    bool valid(SurfacePointer *ptr, const cv::Vec3f &offset = {0,0,0}) override;
    cv::Vec3f loc(SurfacePointer *ptr, const cv::Vec3f &offset = {0,0,0}) override;
    cv::Vec3f coord(SurfacePointer *ptr, const cv::Vec3f &offset = {0,0,0}) override;
    cv::Vec3f normal(SurfacePointer *ptr, const cv::Vec3f &offset = {0,0,0}) override;
    void gen(cv::Mat_<cv::Vec3f> *coords, cv::Mat_<cv::Vec3f> *normals, cv::Size size, SurfacePointer *ptr, float scale, const cv::Vec3f &offset) override = 0;
    float pointTo(SurfacePointer *ptr, const cv::Vec3f &tgt, float th, int max_iters = 1000) override;

protected:
    Surface *_base = nullptr;
};

//might in the future have more properties! or those props are handled in whatever class manages a set of control points ...
class SurfaceControlPoint
{
public:
    SurfaceControlPoint(Surface *base, SurfacePointer *ptr_, const cv::Vec3f &control);
    SurfacePointer *ptr; //ptr to control point in base surface
    cv::Vec3f orig_wp; //the original 3d location where the control point was created
    cv::Vec3f normal; //original normal
    cv::Vec3f control_point; //actual control point location - should be in line with _orig_wp along the normal, but could change if the underlaying surface changes!
};

class ControlPointSurface : public DeltaSurface
{
public:
    ControlPointSurface(Surface *base) : DeltaSurface(base) {};
    void addControlPoint(SurfacePointer *base_ptr, cv::Vec3f control_point);
    void gen(cv::Mat_<cv::Vec3f> *coords, cv::Mat_<cv::Vec3f> *normals, cv::Size size, SurfacePointer *ptr, float scale, const cv::Vec3f &offset) override;

    void setBase(Surface *base);

protected:
    std::vector<SurfaceControlPoint> _controls;
};

class RefineCompSurface : public DeltaSurface
{
public:
    RefineCompSurface(z5::Dataset *ds, ChunkCache *cache, QuadSurface *base = nullptr);
    void gen(cv::Mat_<cv::Vec3f> *coords, cv::Mat_<cv::Vec3f> *normals, cv::Size size, SurfacePointer *ptr, float scale, const cv::Vec3f &offset) override;
    
    float start = 0;
    float stop = -100;
    float step = 2.0;
    float low = 0.1;
    float high = 1.0;
protected:
    z5::Dataset *_ds;
    ChunkCache *_cache;
};

class SurfaceMeta
{
public:
    SurfaceMeta() {};
    SurfaceMeta(const std::filesystem::path &path_, const nlohmann::json &json);
    SurfaceMeta(const std::filesystem::path &path_);
    ~SurfaceMeta();
    void readOverlapping();
    QuadSurface *surface();
    void setSurface(QuadSurface *surf);
    std::string name();
    std::filesystem::path path;
    QuadSurface *_surf = nullptr;
    Rect3D bbox;

    json_metadata metadata;
    std::set<std::string> overlapping_str;
    std::set<SurfaceMeta*> overlapping;
};

Rect3D rect_from_json(const nlohmann::json &json);
bool overlap(SurfaceMeta &a, SurfaceMeta &b, int max_iters = 1000);
bool contains(SurfaceMeta &a, const cv::Vec3f &loc, int max_iters = 1000);
bool contains(SurfaceMeta &a, const std::vector<cv::Vec3f> &locs);
bool contains_any(SurfaceMeta &a, const std::vector<cv::Vec3f> &locs);

//TODO constrain to visible area? or add visible area display?
void find_intersect_segments(std::vector<std::vector<cv::Vec3f>> &seg_vol, std::vector<std::vector<cv::Vec2f>> &seg_grid, const cv::Mat_<cv::Vec3f> &points, PlaneSurface *plane, const cv::Rect &plane_roi, float step, int min_tries = 10);

float min_loc(const cv::Mat_<cv::Vec3f> &points, cv::Vec2f &loc, cv::Vec3f &out, const std::vector<cv::Vec3f> &tgts, const std::vector<float> &tds, PlaneSurface *plane, float init_step = 16.0, float min_step = 0.125);

QuadSurface *grow_surf_from_surfs(SurfaceMeta *seed, const std::vector<SurfaceMeta*> &surfs_v, const nlohmann::json &params, float voxelsize = 1.0);
float pointTo(cv::Vec2f &loc, const cv::Mat_<cv::Vec3d> &points, const cv::Vec3f &tgt, float th, int max_iters, float scale);
float pointTo(cv::Vec2f &loc, const cv::Mat_<cv::Vec3f> &points, const cv::Vec3f &tgt, float th, int max_iters, float scale);

void write_overlapping_json(const std::filesystem::path& seg_path, const std::set<std::string>& overlapping_names);
std::set<std::string> read_overlapping_json(const std::filesystem::path& seg_path);
