#include "vc/core/util/surface_metrics.hpp"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

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

            // if (!g_debug_image.empty()) {
            //     cv::Vec2f scale = surface->scale();
            //     g_debug_image.at<cv::Vec3b>(cand_loc_2f[1], cand_loc_2f[0]) = cv::Vec3b(128, 128, 128);
            // }

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

// Iteratively search from random locations until a good intersection is found
cv::Vec2f find_intersection(QuadSurface* surface, const cv::Vec3f& p1, const cv::Vec3f& p2, float& best_dist)
{
    cv::Vec2f best_loc = {-1, -1};
    best_dist = -1.0f;

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

        if (!g_debug_image.empty()) {
            cv::drawMarker(g_debug_image, cv::Point(nominal_loc[0], nominal_loc[1]), cv::Scalar(255, 0, 0), cv::MARKER_CROSS, 5, 1);
        }

        float dist = find_intersection_direct(surface, cand_loc_abs, p1, p2, 16.0f, 0.001f, center_in_points);

        if (dist < 0)
            continue;

        if (dist < 1e-3) { // Found a very good match
            best_dist = dist;
            return cand_loc_abs;
        }

        if (best_dist == -1.0f || dist < best_dist) {
            best_dist = dist;
            best_loc = cand_loc_abs;
        }
    }
    return best_loc;
}


double point_winding_error(const ChaoVis::VCCollection& collection, QuadSurface* surface, const cv::Mat_<float>& winding)
{
    g_debug_image = cv::Mat::zeros(winding.size(), CV_8UC3);

    for (const auto& pair : collection.getAllCollections()) {
        const auto& coll = pair.second;

        std::vector<ChaoVis::ColPoint> points_with_winding;
        for (const auto& p_pair : coll.points) {
            if (!std::isnan(p_pair.second.winding_annotation)) {
                points_with_winding.push_back(p_pair.second);
            }
        }

        if (points_with_winding.size() < 2) {
            continue;
        }

        std::sort(points_with_winding.begin(), points_with_winding.end(), [](const auto& a, const auto& b) {
            return a.winding_annotation < b.winding_annotation;
        });

        for (size_t i = 0; i < points_with_winding.size() - 1; ++i) {
            const auto& p1_info = points_with_winding[i];
            const auto& p2_info = points_with_winding[i+1];

            float dist = -1.0f;
            cv::Vec2f intersection_loc = find_intersection(surface, p1_info.p, p2_info.p, dist);

            if (intersection_loc[0] >= 0) {
                TrivialSurfacePointer zero_ptr({0,0,0});
                cv::Vec3f center_in_points = surface->loc_raw(&zero_ptr);
                cv::Vec3f ptr_loc = cv::Vec3f(intersection_loc[0], intersection_loc[1], 0) - center_in_points;
                TrivialSurfacePointer ptr(ptr_loc);
                cv::Vec3f intersection_3d = surface->coord(&ptr);
                float intersection_winding = winding(intersection_loc[1], intersection_loc[0]);

                cv::Vec2f scale = surface->scale();
                std::cout << "Intersection for line " << p1_info.id << " -> " << p2_info.id << std::endl;
                std::cout << "  Location (2D grid): " << intersection_loc << std::endl;
                std::cout << "  Location (3D world): " << intersection_3d << std::endl;
                std::cout << "  Winding: " << intersection_winding << std::endl;
                std::cout << "  Distance to line: " << dist << std::endl;

                // cv::Point center(intersection_loc[0] / scale[0], intersection_loc[1] / scale[1]);
                cv::Point center(intersection_loc[0], intersection_loc[1]);
                cv::Vec3f color_f = coll.color;
                cv::Scalar color(color_f[2] * 255, color_f[1] * 255, color_f[0] * 255); // BGR for OpenCV
                cv::circle(g_debug_image, center, 3, color, -1);

                std::string text = std::to_string(intersection_winding) + "/" + std::to_string(dist);
                cv::putText(g_debug_image, text, center + cv::Point(5, 5), cv::FONT_HERSHEY_SIMPLEX, 0.4, color, 1);

            } else {
                std::cout << "Intersection for line " << p1_info.id << " -> " << p2_info.id << " NOT FOUND" << std::endl;
            }
        }
    }

    cv::imwrite("dbg.tif", g_debug_image);
    std::cout << "Debug visualization saved to dbg.tif" << std::endl;

    g_debug_image.release();
    return 0.0;
}

} // namespace vc::apps
