#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>
#include <boost/program_options.hpp>
#include <cmath>
#include <vector>
#include <string>
#include <iostream>
#include <limits>
#include <mutex>
#include <filesystem>
#include <atomic>
#include <iomanip>
#ifdef _OPENMP
#include <omp.h>
#endif

namespace po = boost::program_options;

struct Config {
    float lambda_ = 1.0f;     // Edge threshold parameter
    float sigma_ = 3.0f;      // Gaussian smoothing for gradients
    float rho_ = 5.0f;        // Gaussian smoothing for structure tensor
    float step_size_ = 0.24f; // Diffusion time step (<= 0.25)
    float m_ = 1.0f;          // Exponent for diffusivity
    int   num_steps_ = 100;   // Iterations
    int   downsample_ = 1;    // Downsample factor (>=1)
    int   dilate_ = 0;        // Dilation radius in pixels (>=0)
    bool  apply_threshold_ = false; // Binarize output
    bool  use_otsu_ = false;        // If true, use Otsu; else use threshold_value_
    double threshold_value_ = 0.0;  // Threshold in [0,255]
    int   remove_small_objects_ = 250; // Minimum area (pixels). 0 disables.
    int   jobs_ = 1;                  // Parallel files processed concurrently in folder mode
    bool  show_progress_ = true;      // Per-iteration inline progress
};

// Constants (match Python)
static constexpr float EPS   = static_cast<float>(std::ldexp(1.0, -52)); // 2^-52
static constexpr float GAMMA = 0.01f;                                     // minimum diffusivity
static constexpr float CM    = 7.2848f;                                   // exponential constant

static inline int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static std::vector<float> gaussian_kernel_1d(float sigma) {
    if (sigma <= 0.f) return {1.f};
    int radius = static_cast<int>(std::ceil(3.0f * sigma));
    int size = 2 * radius + 1;
    std::vector<float> k(size);
    float denom = 2.0f * sigma * sigma;
    float sum = 0.f;
    for (int i = 0; i < size; ++i) {
        int x = i - radius;
        float v = std::exp(-(x * x) / denom);
        k[i] = v;
        sum += v;
    }
    for (int i = 0; i < size; ++i) k[i] /= sum;
    return k;
}

static void gaussian_blur(const std::vector<float>& src, int H, int W, float sigma, std::vector<float>& dst) {
    if (sigma <= 0.f) {
        if (&dst != &src) { dst = src; }
        return;
    }
    cv::Mat srcM(H, W, CV_32F, const_cast<float*>(src.data()));
    dst.resize(H * W);
    cv::Mat dstM(H, W, CV_32F, dst.data());
    cv::GaussianBlur(srcM, dstM, cv::Size(0, 0), sigma, sigma, cv::BORDER_REPLICATE);
}

static void compute_gradients(const std::vector<float>& img, int H, int W,
                              std::vector<float>& gx, std::vector<float>& gy) {
    gx.assign(H * W, 0.f);
    gy.assign(H * W, 0.f);
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int xm = clampi(x - 1, 0, W - 1);
            int xp = clampi(x + 1, 0, W - 1);
            int ym = clampi(y - 1, 0, H - 1);
            int yp = clampi(y + 1, 0, H - 1);
            gx[y * W + x] = 0.5f * (img[y * W + xp] - img[y * W + xm]);
            gy[y * W + x] = 0.5f * (img[yp * W + x] - img[ym * W + x]);
        }
    }
}

static void compute_structure_tensor(const std::vector<float>& gx, const std::vector<float>& gy, int H, int W, float rho,
                                     std::vector<float>& s11, std::vector<float>& s12, std::vector<float>& s22) {
    std::vector<float> gx2(H * W), gy2(H * W), gxy(H * W);
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < H * W; ++i) {
        float gxv = gx[i];
        float gyv = gy[i];
        gx2[i] = gxv * gxv;
        gy2[i] = gyv * gyv;
        gxy[i] = gxv * gyv;
    }
    gaussian_blur(gx2, H, W, rho, s11);
    gaussian_blur(gxy, H, W, rho, s12);
    gaussian_blur(gy2, H, W, rho, s22);
}

static void compute_alpha(const std::vector<float>& s11, const std::vector<float>& s12, const std::vector<float>& s22,
                          int HW, std::vector<float>& alpha) {
    alpha.resize(HW);
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < HW; ++i) {
        float a = s11[i] - s22[i];
        float b = s12[i];
        alpha[i] = std::sqrt(a * a + 4.0f * b * b);
    }
}

static void compute_c2(const std::vector<float>& alpha, float lambda_, float m, int HW, std::vector<float>& c2) {
    c2.resize(HW);
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < HW; ++i) {
        float h1 = (alpha[i] + EPS) / lambda_;
        float h2 = (std::abs(m - 1.0f) < 1e-10f) ? h1 : std::pow(h1, m);
        float h3 = std::exp(-CM / h2);
        c2[i] = GAMMA + (1.0f - GAMMA) * h3;
    }
}

static void compute_diffusion_tensor(const std::vector<float>& s11, const std::vector<float>& s12, const std::vector<float>& s22,
                                     const std::vector<float>& alpha, const std::vector<float>& c2,
                                     int HW,
                                     std::vector<float>& d11, std::vector<float>& d12, std::vector<float>& d22) {
    d11.resize(HW); d12.resize(HW); d22.resize(HW);
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < HW; ++i) {
        float dd = (c2[i] - GAMMA) * (s11[i] - s22[i]) / (alpha[i] + EPS);
        d11[i] = 0.5f * (GAMMA + c2[i] + dd);
        d12[i] = (GAMMA - c2[i]) * s12[i] / (alpha[i] + EPS);
        d22[i] = 0.5f * (GAMMA + c2[i] - dd);
    }
}

static void diffusion_step(const std::vector<float>& img, const std::vector<float>& d11, const std::vector<float>& d12, const std::vector<float>& d22,
                           int H, int W, float step_size, std::vector<float>& img_out) {
    img_out.assign(H * W, 0.f);
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int i = y * W + x;
            int yN = clampi(y - 1, 0, H - 1);
            int yS = clampi(y + 1, 0, H - 1);
            int xW = clampi(x - 1, 0, W - 1);
            int xE = clampi(x + 1, 0, W - 1);

            int idxC  = i;
            int idxN  = yN * W + x;
            int idxS  = yS * W + x;
            int idxW  = y * W + xW;
            int idxE  = y * W + xE;
            int idxNW = yN * W + xW;
            int idxNE = yN * W + xE;
            int idxSW = yS * W + xW;
            int idxSE = yS * W + xE;

            float img_c = img[idxC];
            float img_n = img[idxN];
            float img_s = img[idxS];
            float img_w = img[idxW];
            float img_e = img[idxE];
            float img_nw = img[idxNW];
            float img_ne = img[idxNE];
            float img_sw = img[idxSW];
            float img_se = img[idxSE];

            float d11_c = d11[idxC];
            float d11_n = d11[idxN];
            float d11_s = d11[idxS];

            float d22_c = d22[idxC];
            float d22_w = d22[idxW];
            float d22_e = d22[idxE];

            float d12_c = d12[idxC];
            float d12_n = d12[idxN];
            float d12_s = d12[idxS];
            float d12_w = d12[idxW];
            float d12_e = d12[idxE];

            float c_cop = d22_c + d22_w; // (i,j) + (i,j-1)
            float a_amo = d11_s + d11_c; // (i+1,j) + (i,j)
            float a_apo = d11_n + d11_c; // (i-1,j) + (i,j)
            float c_com = d22_c + d22_e; // (i,j) + (i,j+1)

            float first_deriv = (
                c_cop * img_w +
                a_amo * img_s -
                (a_amo + a_apo + c_com + c_cop) * img_c +
                a_apo * img_n +
                c_com * img_e
            );

            float bmo = d12_s;
            float bop = d12_w;
            float bpo = d12_n;
            float bom = d12_e;

            float second_deriv = (
                -1.0f * ((bmo + bop) * img_sw + (bpo + bom) * img_ne) +
                (bpo + bop) * img_nw +
                (bmo + bom) * img_se
            );

            img_out[idxC] = img_c + step_size * (0.5f * first_deriv + 0.25f * second_deriv);
        }
    }
}

static std::mutex g_print_mtx;

static void ced_run(const cv::Mat& input, cv::Mat& output, const Config& cfg,
                    const char* progress_label = nullptr, int progress_mod = 1) {

    const int H = input.rows;
    const int W = input.cols;
    std::vector<float> img(H * W);
    if (input.type() == CV_8UC1) {
        #pragma omp parallel for schedule(static)
        for (int y = 0; y < H; ++y) {
            const uint8_t* row = input.ptr<uint8_t>(y);
            for (int x = 0; x < W; ++x) img[y * W + x] = static_cast<float>(row[x]);
        }
    } else if (input.type() == CV_16UC1) {
        #pragma omp parallel for schedule(static)
        for (int y = 0; y < H; ++y) {
            const uint16_t* row = input.ptr<uint16_t>(y);
            for (int x = 0; x < W; ++x) img[y * W + x] = static_cast<float>(row[x]);
        }
    } else if (input.type() == CV_32FC1) {
        #pragma omp parallel for schedule(static)
        for (int y = 0; y < H; ++y) {
            const float* row = input.ptr<float>(y);
            for (int x = 0; x < W; ++x) img[y * W + x] = row[x];
        }
    } else {
        throw std::runtime_error("Unsupported input image type; use 8U, 16U, or 32F single-channel TIFF");
    }


    std::vector<float> img_smooth(H * W), gx(H * W), gy(H * W), s11(H * W), s12(H * W), s22(H * W),
                       alpha(H * W), c2(H * W), d11(H * W), d12(H * W), d22(H * W), img_new(H * W);
    for (int step = 0; step < cfg.num_steps_; ++step) {
        if (cfg.show_progress_) {
            if (progress_label == nullptr) {
                std::cout << "\rStep " << (step + 1) << "/" << cfg.num_steps_ << std::flush;
            } else {
                bool do_print = (progress_mod <= 1) || ((step % progress_mod) == 0) || (step + 1 == cfg.num_steps_);
                if (do_print) {
                    std::lock_guard<std::mutex> lock(g_print_mtx);
                    std::cout << "[" << progress_label << "] Step " << (step + 1)
                              << "/" << cfg.num_steps_ << std::endl;
                }
            }
        }

        gaussian_blur(img, H, W, cfg.sigma_, img_smooth);
        compute_gradients(img_smooth, H, W, gx, gy);
        compute_structure_tensor(gx, gy, H, W, cfg.rho_, s11, s12, s22);
        compute_alpha(s11, s12, s22, H * W, alpha);
        compute_c2(alpha, cfg.lambda_, cfg.m_, H * W, c2);
        compute_diffusion_tensor(s11, s12, s22, alpha, c2, H * W, d11, d12, d22);
        diffusion_step(img, d11, d12, d22, H, W, cfg.step_size_, img_new);
        img.swap(img_new);
    }
    if (cfg.show_progress_) {
        if (progress_label == nullptr) {
            std::cout << "\nDiffusion complete!" << std::endl;
        } else {
            std::lock_guard<std::mutex> lock(g_print_mtx);
            std::cout << "[" << progress_label << "] Complete" << std::endl;
        }
    }

    output.create(H, W, input.type());
    if (input.type() == CV_8UC1) {
        #pragma omp parallel for schedule(static)
        for (int y = 0; y < H; ++y) {
            uint8_t* row = output.ptr<uint8_t>(y);
            for (int x = 0; x < W; ++x) {
                float v = img[y * W + x];
                if (v < 1.f) v = 0.f; // set values below 1 to 0 because there is tons of irritating noise from the CED output at very low vals
                v = std::min(std::max(v, 0.0f), 255.0f);
                row[x] = static_cast<uint8_t>(std::lround(v));
            }
        }
    } else if (input.type() == CV_16UC1) {
        #pragma omp parallel for schedule(static)
        for (int y = 0; y < H; ++y) {
            uint16_t* row = output.ptr<uint16_t>(y);
            for (int x = 0; x < W; ++x) {
                float v = img[y * W + x];
                v = std::min(std::max(v, 0.0f), 65535.0f);
                row[x] = static_cast<uint16_t>(std::lround(v));
            }
        }
    } else { // CV_32FC1
        #pragma omp parallel for schedule(static)
        for (int y = 0; y < H; ++y) {
            float* row = output.ptr<float>(y);
            for (int x = 0; x < W; ++x) row[x] = img[y * W + x];
        }
    }
}

static bool find_nonzero_bbox(const cv::Mat& img, cv::Rect& bbox) {
    const int H = img.rows, W = img.cols;
    int minx = W, miny = H, maxx = -1, maxy = -1;
    if (img.type() == CV_8UC1) {
        for (int y = 0; y < H; ++y) {
            const uint8_t* row = img.ptr<uint8_t>(y);
            for (int x = 0; x < W; ++x) if (row[x] != 0) { if (x < minx) minx = x; if (y < miny) miny = y; if (x > maxx) maxx = x; if (y > maxy) maxy = y; }
        }
    } else if (img.type() == CV_16UC1) {
        for (int y = 0; y < H; ++y) {
            const uint16_t* row = img.ptr<uint16_t>(y);
            for (int x = 0; x < W; ++x) if (row[x] != 0) { if (x < minx) minx = x; if (y < miny) miny = y; if (x > maxx) maxx = x; if (y > maxy) maxy = y; }
        }
    } else if (img.type() == CV_32FC1) {
        for (int y = 0; y < H; ++y) {
            const float* row = img.ptr<float>(y);
            for (int x = 0; x < W; ++x) if (row[x] != 0.0f) { if (x < minx) minx = x; if (y < miny) miny = y; if (x > maxx) maxx = x; if (y > maxy) maxy = y; }
        }
    } else {
        return false;
    }
    if (maxx < 0) return false;
    bbox = cv::Rect(minx, miny, maxx - minx + 1, maxy - miny + 1);
    return true;
}

static cv::Mat binarize_and_dilate(const cv::Mat& img, int dilate_radius) {
    // Binary mask: any nonzero pixel -> 255
    cv::Mat mask;
    cv::compare(img, 0, mask, cv::CMP_GT); // mask is 8U, values 0 or 255
    if (dilate_radius > 0) {
        int k = 2 * dilate_radius + 1;
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k));
        cv::dilate(mask, mask, kernel, cv::Point(-1, -1), 1);
    }
    return mask;
}

static cv::Mat process_one_image(const cv::Mat& img, const Config& cfg, const char* progress_label = nullptr) {
    // 1) Binarize and optionally dilate to build a mask
    cv::Mat mask = binarize_and_dilate(img, cfg.dilate_);

    // 2) Compute bbox from mask; if empty, return original
    cv::Rect bbox;
    if (!find_nonzero_bbox(mask, bbox)) {
        return img.clone();
    }
    int H = img.rows, W = img.cols;
    int margin = static_cast<int>(std::ceil(3.f * std::max(cfg.sigma_, cfg.rho_))) + 2;
    int x0 = std::max(0, bbox.x - margin);
    int y0 = std::max(0, bbox.y - margin);
    int x1 = std::min(W, bbox.x + bbox.width + margin);
    int y1 = std::min(H, bbox.y + bbox.height + margin);
    cv::Rect ext(x0, y0, x1 - x0, y1 - y0);

    cv::Mat region = img(ext).clone();
    cv::Mat mask_ext = mask(ext);
    // Build a binarized input image: inside mask -> max, else 0
    cv::Mat region_bin = cv::Mat::zeros(region.size(), region.type());
    if (region.type() == CV_8UC1) {
        region_bin.setTo(255, mask_ext);
    } else if (region.type() == CV_16UC1) {
        region_bin.setTo(65535, mask_ext);
    } else { // CV_32FC1
        region_bin.setTo(1.0f, mask_ext);
    }

    cv::Mat processed;
    if (cfg.downsample_ > 1) {
        int f = cfg.downsample_;
        int dW = std::max(1, (ext.width  + f - 1) / f);
        int dH = std::max(1, (ext.height + f - 1) / f);
        cv::Mat region_ds; cv::resize(region_bin, region_ds, cv::Size(dW, dH), 0, 0, cv::INTER_AREA);
        Config cfg_ds = cfg; cfg_ds.sigma_ = cfg.sigma_ / f; cfg_ds.rho_ = cfg.rho_ / f; if (cfg_ds.sigma_ < 0.f) cfg_ds.sigma_ = 0.f; if (cfg_ds.rho_ < 0.f) cfg_ds.rho_ = 0.f;
        cv::Mat out_ds; ced_run(region_ds, out_ds, cfg_ds, progress_label, std::max(1, cfg_ds.num_steps_/10));
        cv::resize(out_ds, processed, region.size(), 0, 0, cv::INTER_LINEAR);
    } else {
        ced_run(region_bin, processed, cfg, progress_label, std::max(1, cfg.num_steps_/10));
    }

    cv::Mat out = img.clone();
    int ox = bbox.x - ext.x; int oy = bbox.y - ext.y;
    cv::Mat inROI = processed(cv::Rect(ox, oy, bbox.width, bbox.height));
    inROI.copyTo(out(bbox));
    return out;
}

static bool is_directory(const std::string& p) {
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::exists(p, ec) && fs::is_directory(p, ec);
}

static bool ensure_dir(const std::string& p) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(p, ec)) return fs::is_directory(p, ec);
    return fs::create_directories(p, ec);
}

static void remove_small_objects(cv::Mat& bin, int min_area) {
    if (min_area <= 0) return;
    CV_Assert(bin.type() == CV_8UC1);
    // Connected components on binary 0/255 image (treat >0 as 1)
    cv::Mat labels, stats, centroids;
    int n = cv::connectedComponentsWithStats(bin, labels, stats, centroids, 8, CV_32S);
    if (n <= 1) return; // only background
    // Build keep mask
    std::vector<uint8_t> keep(n, 0);
    keep[0] = 0; // background
    for (int i = 1; i < n; ++i) {
        int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area >= min_area) keep[i] = 255;
    }
    // Write back
    for (int y = 0; y < bin.rows; ++y) {
        const int* lrow = labels.ptr<int>(y);
        uint8_t* brow = bin.ptr<uint8_t>(y);
        for (int x = 0; x < bin.cols; ++x) brow[x] = keep[lrow[x]];
    }
}

static cv::Mat to_uint8_scaled_and_threshold(const cv::Mat& in, const Config& cfg) {
    cv::Mat u8;
    if (in.type() == CV_8UC1) {
        u8 = in.clone();
    } else if (in.type() == CV_16UC1) {
        // Map [0..65535] -> [0..255]
        in.convertTo(u8, CV_8U, 1.0 / 257.0, 0.0);
    } else if (in.type() == CV_32FC1) {
        // Map [0..1] -> [0..255]
        in.convertTo(u8, CV_8U, 255.0, 0.0);
    } else {
        throw std::runtime_error("Unsupported image type for output conversion");
    }

    if (cfg.apply_threshold_) {
        cv::Mat bin;
        if (cfg.use_otsu_) {
            cv::threshold(u8, bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
        } else {
            double t = std::max(0.0, std::min(255.0, cfg.threshold_value_));
            cv::threshold(u8, bin, t, 255, cv::THRESH_BINARY);
        }
        // Remove small connected components (default 250 px). 0 disables.
        remove_small_objects(bin, cfg.remove_small_objects_);
        return bin;
    }
    return u8;
}

int main(int argc, char** argv) {
    std::string in_path, out_path;
    Config cfg;
    int num_threads = -1;

    try {
        po::options_description desc("CED2D options");
        desc.add_options()
            ("help,h", "Show help")
            ("input,i", po::value<std::string>(&in_path)->required(), "Input path: TIFF file or directory of TIFFs")
            ("output,o", po::value<std::string>(&out_path)->required(), "Output path: TIFF file or directory (if input is a directory)")
            ("lambda", po::value<float>(&cfg.lambda_)->default_value(1.0f), "Edge threshold parameter")
            ("sigma", po::value<float>(&cfg.sigma_)->default_value(3.0f), "Gaussian sigma for gradients")
            ("rho", po::value<float>(&cfg.rho_)->default_value(5.0f), "Gaussian sigma for structure tensor")
            ("step-size", po::value<float>(&cfg.step_size_)->default_value(0.24f), "Diffusion step size (<=0.25)")
            ("m", po::value<float>(&cfg.m_)->default_value(1.0f), "Exponent m for diffusivity")
            ("num-steps", po::value<int>(&cfg.num_steps_)->default_value(100), "Number of diffusion steps")
            ("downsample", po::value<int>(&cfg.downsample_)->default_value(1), "Downsample factor (>=1)")
            ("dilate", po::value<int>(&cfg.dilate_)->default_value(0), "Dilate radius (pixels, >=0) after binarization")
        ;

        // threshold option: optional value, if no value -> Otsu
        double threshold_opt = std::numeric_limits<double>::quiet_NaN();
        desc.add_options()("threshold", po::value<double>(&threshold_opt)->implicit_value(std::numeric_limits<double>::quiet_NaN()),
                           "Binarize output as uint8; if value omitted => Otsu, else numeric threshold [0..255]");

        // remove-small-objects: optional value; default and implicit both 250. Set to 0 to disable.
        desc.add_options()("remove-small-objects", po::value<int>(&cfg.remove_small_objects_)->default_value(250)->implicit_value(250),
                           "Remove connected components smaller than N pixels from binary output (0 disables)");

        desc.add_options()
            ("threads", po::value<int>(&num_threads)->default_value(-1), "OpenMP threads for per-image compute (-1 auto)")
            ("jobs", po::value<int>(&cfg.jobs_)->default_value(1), "Parallel file jobs in folder mode (>=1)")
        ;

        if (argc == 1) {
            std::cout << desc << std::endl;
            return 0;
        }

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        if (vm.count("help")) {
            std::cout << desc << std::endl;
            return 0;
        }
        po::notify(vm);

        if (cfg.step_size_ > 0.25f) {
            std::cerr << "Warning: step-size > 0.25 may be unstable; clamping to 0.25\n";
            cfg.step_size_ = 0.25f;
        }
        if (cfg.downsample_ < 1) cfg.downsample_ = 1;
        if (cfg.dilate_ < 0) cfg.dilate_ = 0;
        if (vm.count("threshold")) {
            cfg.apply_threshold_ = true;
            cfg.use_otsu_ = std::isnan(threshold_opt);
            if (!cfg.use_otsu_) cfg.threshold_value_ = threshold_opt;
        }

        int jobs = std::max(1, cfg.jobs_);
        int compute_threads = 1;
        #ifdef _OPENMP
        if (jobs > 1) {
            compute_threads = 1; // parallelize over files; keep per-image compute single-threaded
        } else {
            compute_threads = (num_threads > 0) ? num_threads : omp_get_max_threads();
        }
        omp_set_num_threads(compute_threads);
        #else
        (void)num_threads;
        compute_threads = 1;
        #endif

        const bool dir_mode = is_directory(in_path);
        if (dir_mode) {
            // Folder mode: process all .tif/.tiff files
            if (!ensure_dir(out_path)) {
                std::cerr << "Cannot create/open output directory: " << out_path << std::endl;
                return 1;
            }
            std::vector<cv::String> files;
            cv::glob(in_path + "/*.tif", files, false);
            std::vector<cv::String> files_tiff;
            cv::glob(in_path + "/*.tiff", files_tiff, false);
            files.insert(files.end(), files_tiff.begin(), files_tiff.end());
            if (files.empty()) {
                std::cerr << "No TIFF files found in directory: " << in_path << std::endl;
                return 1;
            }
            std::cout << "Found " << files.size() << " TIFF files in " << in_path << "\n";
            std::vector<int> params = { cv::IMWRITE_TIFF_COMPRESSION, 32773 }; // packbits
            jobs = std::max(1, cfg.jobs_);
            bool multi = jobs > 1;
            std::cout << "Folder jobs: " << jobs << " (per-image compute threads: " << compute_threads << ")\n";

            const int total = static_cast<int>(files.size());
            std::atomic<int> completed{0};
            {
                std::lock_guard<std::mutex> lock(g_print_mtx);
                std::cout << std::fixed << std::setprecision(1)
                          << "\rProgress: 0/" << total << " (0.0%), remaining " << total << std::flush;
            }

            #ifdef _OPENMP
            if (multi) {
                #pragma omp parallel for num_threads(jobs) schedule(dynamic)
                for (int i = 0; i < static_cast<int>(files.size()); ++i) {
                    const auto f = files[i];
                    cv::Mat img = cv::imread(f, cv::IMREAD_UNCHANGED);
                    namespace fs = std::filesystem;
                    std::string base = fs::path(f).filename().string();
                    if (img.empty() || img.channels() != 1 || img.dims != 2) {
                        int done = ++completed;
                        int rem = total - done;
                        double pct = 100.0 * (double)done / (double)total;
                        std::lock_guard<std::mutex> lock(g_print_mtx);
                        std::cout << std::fixed << std::setprecision(1)
                                  << "\rProgress: " << done << "/" << total << " (" << pct << "%), remaining " << rem << std::flush;
                        continue;
                    }
                    Config local_cfg = cfg; local_cfg.show_progress_ = false;
                    cv::Mat out = process_one_image(img, local_cfg);
                    cv::Mat out_u8 = to_uint8_scaled_and_threshold(out, local_cfg);
                    std::string out_file = (fs::path(out_path) / base).string();
                    (void)cv::imwrite(out_file, out_u8, params);
                    int done = ++completed;
                    int rem = total - done;
                    double pct = 100.0 * (double)done / (double)total;
                    std::lock_guard<std::mutex> lock(g_print_mtx);
                    std::cout << std::fixed << std::setprecision(1)
                              << "\rProgress: " << done << "/" << total << " (" << pct << "%), remaining " << rem << std::flush;
                }
            } else
            #endif
            {
                const int total = static_cast<int>(files.size());
                int completed = 0;
                for (size_t idx = 0; idx < files.size(); ++idx) {
                    const auto& f = files[idx];
                    namespace fs = std::filesystem;
                    std::string base = fs::path(f).filename().string();
                    cv::Mat img = cv::imread(f, cv::IMREAD_UNCHANGED);
                    if (!(img.empty() || img.channels() != 1 || img.dims != 2)) {
                        cv::Mat out = process_one_image(img, cfg);
                        cv::Mat out_u8 = to_uint8_scaled_and_threshold(out, cfg);
                        std::string out_file = (fs::path(out_path) / base).string();
                        (void)cv::imwrite(out_file, out_u8, params);
                    }
                    int done = ++completed;
                    int rem = total - done;
                    double pct = 100.0 * (double)done / (double)total;
                    std::cout << std::fixed << std::setprecision(1)
                              << "\rProgress: " << done << "/" << total << " (" << pct << "%), remaining " << rem << std::flush;
                }
            }
            std::cout << std::endl << "Done folder processing." << std::endl;
        } else {
            cv::Mat img = cv::imread(in_path, cv::IMREAD_UNCHANGED);
            if (img.empty()) {
                std::cerr << "Failed to read input TIFF: " << in_path << std::endl;
                return 1;
            }
            if (img.channels() != 1) {
                std::cerr << "Only single-channel (grayscale) 2D TIFFs are supported" << std::endl;
                return 1;
            }
            if (img.dims != 2) {
                std::cerr << "Only 2D TIFFs are supported" << std::endl;
                return 1;
            }

            cv::Mat out = process_one_image(img, cfg);
            cv::Mat out_u8 = to_uint8_scaled_and_threshold(out, cfg);

            std::vector<int> params = { cv::IMWRITE_TIFF_COMPRESSION, 32773 };
            if (!cv::imwrite(out_path, out_u8, params)) {
                std::cerr << "Failed to write output TIFF: " << out_path << std::endl;
                return 1;
            }
            std::cout << "Saved: " << out_path << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
