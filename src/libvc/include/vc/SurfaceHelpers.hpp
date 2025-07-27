#pragma once

#include <opencv2/core.hpp>

// External declarations for configurable parameters
extern float local_cost_inl_th;
extern float same_surface_th;
extern float straight_weight;
extern float straight_min_count;
extern int inlier_base_threshold;

cv::Mat_<cv::Vec3f> upsample_with_grounding(cv::Mat_<cv::Vec3f> &small, cv::Mat_<cv::Vec2f> &locs, const cv::Size &tgt_size, const cv::Mat_<cv::Vec3f> &points, double sx, double sy);
void refine_normal(const std::vector<std::pair<cv::Vec2i,cv::Vec3f>> &refs, cv::Vec3f &point, cv::Vec3f &normal, cv::Vec3f &vx, cv::Vec3f &vy, const std::vector<float> &ws);
