#include "vc/core/types/ChunkedTensor.hpp"

#include "z5/filesystem/handle.hxx"
#include "z5/factory.hxx"

#include <xtensor/xview.hpp>

#include <opencv2/highgui.hpp>

struct passTroughComputor
{
    enum {BORDER = 0};
    enum {CHUNK_SIZE = 32};
    enum {FILL_V = 0};
    template <typename T, typename E> void compute(const T &large, T &small)
    {
        int low = int(BORDER);
        int high = int(BORDER)+int(CHUNK_SIZE);
        small = view(large, xt::range(low,high),xt::range(low,high),xt::range(low,high));
    }
};

template <typename E>
E _max_d_ign(const E &a, const E &b)
{
    if (a == E(-1))
        return b;
    if (b == E(-1))
        return a;
    return std::max(a,b);
}

template <typename T, typename E>
void _dist_iteration(T &from, T &to, int s)
{
    E magic = -1;
#pragma omp parallel for
    for(int k=0;k<s;k++)
        for(int j=0;j<s;j++)
            for(int i=0;i<s;i++) {
                E dist = from(k,j,i);
                if (dist == magic) {
                    if (k) dist = _max_d_ign(dist, from(k-1,j,i));
                    if (k < s-1) dist = _max_d_ign(dist, from(k+1,j,i));
                    if (j) dist = _max_d_ign(dist, from(k,j-1,i));
                    if (j < s-1) dist = _max_d_ign(dist, from(k,j+1,i));
                    if (i) dist = _max_d_ign(dist, from(k,j,i-1));
                    if (i < s-1) dist = _max_d_ign(dist, from(k,j,i+1));
                    if (dist != magic)
                        to(k,j,i) = dist+1;
                    else
                        to(k,j,i) = dist;
                }
                else
                    to(k,j,i) = dist;

            }
}

template <typename T, typename E>
T distance_transform(const T &chunk, int steps, int size)
{
    T c1 = xt::empty<E>(chunk.shape());
    T c2 = xt::empty<E>(chunk.shape());

    c1 = chunk;

    E magic = -1;

    for(int n=0;n<steps/2;n++) {
        _dist_iteration<T,E>(c1,c2,size);
        _dist_iteration<T,E>(c2,c1,size);
    }

    #pragma omp parallel for
    for(int z=0;z<size;z++)
        for(int y=0;y<size;y++)
            for(int x=0;x<size;x++)
                if (c1(z,y,x) == magic)
                    c1(z,y,x) = steps;

    return c1;
}

struct thresholdedDistance
{
    enum {BORDER = 16};
    enum {CHUNK_SIZE = 32};
    enum {FILL_V = 0};
    template <typename T, typename E> void compute(const T &large, T &small)
    {
        T outer = xt::empty<E>(large.shape());

        int s = CHUNK_SIZE+2*BORDER;
        E magic = -1;

#pragma omp parallel for
        for(int z=0;z<s;z++)
            for(int y=0;y<s;y++)
                for(int x=0;x<s;x++)
                    if (large(z,y,x) < 50)
                        outer(z,y,x) = magic;
                    else
                        outer(z,y,x) = 0;

        outer = distance_transform<T,E>(outer, 15, s);

        int low = int(BORDER);
        int high = int(BORDER)+int(CHUNK_SIZE);

        auto crop_outer = view(outer, xt::range(low,high),xt::range(low,high),xt::range(low,high));

        small = crop_outer;
    }

};

int main(int argc, char *argv[])
{
    assert(argc == 2);

    const char *vol_path = argv[1];
    std::string group_name = "0";
    z5::filesystem::handle::Group group(vol_path, z5::FileMode::FileMode::r);
    z5::filesystem::handle::Dataset ds_handle(group, group_name, nlohmann::json::parse(std::ifstream(std::filesystem::path(vol_path)/group_name/".zarray")).value<std::string>("dimension_separator","."));
    std::unique_ptr<z5::Dataset> ds = z5::filesystem::openDataset(ds_handle);

    // std::cout << ds.get()->shape() << std::endl;
    
    thresholdedDistance compute;

    ChunkCache chunk_cache(10e9);

    Chunked3d<uint8_t,thresholdedDistance> proc_tensor(compute, ds.get(), &chunk_cache);

    int w = ds.get()->shape(2);
    int h = ds.get()->shape(1);

    cv::Mat_<uint8_t> img(cv::Size(w,h),0);
    // Chunked3dAccessor acc(proc_tensor);
    // for(int j=0;j<h-32;j+=32) {
    //     std::cout << j << std::endl;
    //     for(int i=0;i<w-32;i+=32)
    //         for(int y=j;y<j+32;y++)
    //             for(int x=i;x<i+32;x++)
    //                 img(y,x) = acc(4000,y,x);
    //     for(int j=0;j<h;j++) {
    //         std::cout << j << std::endl;
    //         for(int i=0;i<w;i++)
    //             img(j,i) = acc(4000,j,i);
    // }
#pragma omp parallel
    {
        Chunked3dAccessor acc(proc_tensor);
#pragma omp for
        for(int j=2000;j<3000;j++) {
            for(int i=2500;i<4500;i++)
                img(j,i) = acc.safe_at(2000,j,i);
        }

    }

    cv::imwrite("slice.tif", img);

    
    return EXIT_SUCCESS;
}
