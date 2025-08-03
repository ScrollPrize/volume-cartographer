#include "vc/core/util/surface_metrics.hpp"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <nlohmann/json.hpp>

namespace vc::apps
{

cv::Mat g_debug_image;

// Helper to get point-to-line-segment squared distance
static float dist_point_segment_sq(const cv::Vec3f& p, const cv::Vec3f& a, const cv::Vec3f& b) {
    cv::Vec3f ab = b - a;
    cv::Vec3f ap = p - a;
    float t = ap.dot(ab) / ab.dot(ab);
    t = std::max(0.0f, std::min(1.0f, t));
    cv::Vec3f closest_point = a + t * ab;
    cv::Vec3f d = p - closest_point;
    return d.dot(d);
}

// Direct search method to minimize point-line distance from a starting location
float find_intersection_direct(QuadSurface* surface, cv::Vec2f& loc, const cv::Vec3f& p1, const cv::Vec3f& p2, float init_step, float min_step, const cv::Vec3f& center_in_points)
{
    cv::Vec3f ptr_loc = cv::Vec3f(loc[0], loc[1], 0) - center_in_points;
    TrivialSurfacePointer ptr(ptr_loc);
    if (!surface->valid(&ptr)) {
        return -1.0f;
    }

    cv::Mat_<cv::Vec3f> points = surface->rawPoints();
    cv::Rect bounds = {0, 0, points.cols - 1, points.rows - 1};

    bool changed = true;
    cv::Vec3f surface_point = surface->coord(&ptr);
    float best_dist_sq = dist_point_segment_sq(surface_point, p1, p2);
    float current_dist_sq;

    std::vector<cv::Vec2f> search = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    float step = init_step;

    while (true) {
        changed = false;

        for (auto& off : search) {
            cv::Vec2f cand_loc_2f = loc + off * step;

            if (!bounds.contains(cv::Point(cand_loc_2f[0], cand_loc_2f[1])))
                continue;

            cv::Vec3f cand_ptr_loc = cv::Vec3f(cand_loc_2f[0], cand_loc_2f[1], 0) - center_in_points;
            TrivialSurfacePointer cand_ptr(cand_ptr_loc);
            if (!surface->valid(&cand_ptr))
                continue;

            surface_point = surface->coord(&cand_ptr);
            current_dist_sq = dist_point_segment_sq(surface_point, p1, p2);

            if (current_dist_sq < best_dist_sq) {
                changed = true;
                best_dist_sq = current_dist_sq;
                loc = cand_loc_2f;
            }
        }

        if (changed)
            continue;

        step *= 0.5;
        if (step < min_step)
            break;
    }

    return sqrt(best_dist_sq);
}

cv::Vec2f find_closest_intersection(QuadSurface* surface, const cv::Vec3f& p1, const cv::Vec3f& p2, const cv::Vec3f& proximity_point, float& line_dist, float& prox_dist)
{
    cv::Vec2f best_loc = {-1, -1};
    line_dist = -1.0f;
    prox_dist = -1.0f;

    cv::Size s_size = surface->size();
    cv::Vec2f scale = surface->scale();

    TrivialSurfacePointer zero_ptr({0,0,0});
    cv::Vec3f center_in_points = surface->loc_raw(&zero_ptr);
    
    srand(time(NULL));

    for (int i = 0; i < 1000; ++i) { // 1000 random trials
        cv::Vec2f nominal_loc = {
            (float)(rand() % s_size.width),
            (float)(rand() % s_size.height)
        };

        cv::Vec2f cand_loc_abs = { nominal_loc[0] * scale[0], nominal_loc[1] * scale[1] };

        cv::Vec3f ptr_loc = cv::Vec3f(cand_loc_abs[0], cand_loc_abs[1], 0) - center_in_points;
        TrivialSurfacePointer ptr(ptr_loc);
        if (!surface->valid(&ptr)) {
            continue;
        }

        float dist = find_intersection_direct(surface, cand_loc_abs, p1, p2, 16.0f, 0.0001f, center_in_points);

        if (dist < 0 || dist >= 0.01) {
            continue;
        }

        TrivialSurfacePointer res_ptr(cv::Vec3f(cand_loc_abs[0], cand_loc_abs[1], 0) - center_in_points);
        cv::Vec3f intersection_3d = surface->coord(&res_ptr);
        float current_prox_dist = cv::norm(intersection_3d - proximity_point);

        if (prox_dist < 0 || current_prox_dist < prox_dist) {
            prox_dist = current_prox_dist;
            line_dist = dist;
            best_loc = cand_loc_abs;
        }
    }

    return best_loc;
}


nlohmann::json calc_point_metrics(const ChaoVis::VCCollection& collection, QuadSurface* surface, const cv::Mat_<float>& winding)
{
    g_debug_image = cv::Mat::zeros(winding.size(), CV_8UC3);
    nlohmann::json results;

    int total_invalid_intersections = 0;
    int total_correct_winding = 0;
    int total_correct_winding_inv = 0;
    int total_comparisons = 0;
    int total_segments = 0;

    for (const auto& pair : collection.getAllCollections()) {
        const auto& coll = pair.second;
        std::cout << "Processing collection: " << coll.name << std::endl;

        std::vector<ChaoVis::ColPoint> points_with_winding;
        for (const auto& p_pair : coll.points) {
            if (!std::isnan(p_pair.second.winding_annotation)) {
                points_with_winding.push_back(p_pair.second);
            }
        }

        if (points_with_winding.size() < 2) {
            std::cout << "  Not enough annotated points to form a segment, skipping." << std::endl;
            continue;
        }

        std::sort(points_with_winding.begin(), points_with_winding.end(), [](const auto& a, const auto& b) {
            return a.winding_annotation < b.winding_annotation;
        });

        std::vector<float> intersection_windings;
        for (size_t i = 0; i < points_with_winding.size() - 1; ++i) {
            total_segments++;
            const auto& p1_info = points_with_winding[i];
            const auto& p2_info = points_with_winding[i+1];

            float line_dist = -1.0f, prox_dist = -1.0f;
            cv::Vec2f loc = find_closest_intersection(surface, p1_info.p, p2_info.p, p1_info.p, line_dist, prox_dist);

            if (loc[0] < 0) {
                intersection_windings.push_back(NAN);
                total_invalid_intersections++;
                std::cout << "  Point " << p1_info.id << ": NOT FOUND" << std::endl;
            } else {
                float intersection_winding = winding(loc[1], loc[0]);
                intersection_windings.push_back(intersection_winding);
                std::cout << "  Point " << p1_info.id << ": found / " << prox_dist << ", winding: " << intersection_winding << ", ref: " << p1_info.winding_annotation << std::endl;

                cv::Point center(loc[0], loc[1]);
                cv::Vec3f color_f = coll.color;
                cv::Scalar color(color_f[2] * 255, color_f[1] * 255, color_f[0] * 255);
                cv::circle(g_debug_image, center, 3, color, -1);

                std::stringstream stream;
                stream << std::fixed << std::setprecision(2) << intersection_winding;
                std::string text = stream.str() + "/" + std::to_string(line_dist);
                cv::putText(g_debug_image, text, center + cv::Point(5, 5), cv::FONT_HERSHEY_SIMPLEX, 0.4, color, 1);
            }
        }

        if (points_with_winding.size() >= 3) {
            for (size_t i = 0; i < intersection_windings.size() - 1; ++i) {
                total_comparisons++;
                float prev_intersection = intersection_windings[i];
                float curr_intersection = intersection_windings[i+1];

                if (std::isnan(prev_intersection) || std::isnan(curr_intersection)) {
                    continue;
                }

                float annotated_winding_diff = points_with_winding[i+1].winding_annotation - points_with_winding[i].winding_annotation;
                float sampled_winding_diff = curr_intersection - prev_intersection;

                if (std::abs(sampled_winding_diff - annotated_winding_diff) < 0.1) {
                    total_correct_winding++;
                } else if (std::abs(-sampled_winding_diff - annotated_winding_diff) < 0.1) {
                    total_correct_winding_inv++;
                }
            }
        }
    }
    
    if (total_segments > 0) {
        results["surface_missing_fraction"] = (float)total_invalid_intersections / total_segments;
    }

    if (total_comparisons > 0) {
        results["winding_error_fraction"] = (float)(total_comparisons - std::max(total_correct_winding, total_correct_winding_inv)) / total_comparisons;
    }

    cv::imwrite("dbg.tif", g_debug_image);
    std::cout << "Debug visualization saved to dbg.tif" << std::endl;

    g_debug_image.release();
    return results;
}

} // namespace vc::apps
