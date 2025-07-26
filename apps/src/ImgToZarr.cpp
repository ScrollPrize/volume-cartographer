#include <nlohmann/json.hpp>

#include <jxl/codestream_header.h>
#include <jxl/decode.h>
#include <jxl/decode_cxx.h>
#include <jxl/resizable_parallel_runner.h>
#include <jxl/resizable_parallel_runner_cxx.h>
#include <jxl/types.h>

#include "vc/core/util/xtensor_include.hpp"
#include XTENSORINCLUDE(containers, xadapt.hpp)
#include XTENSORINCLUDE(views, xview.hpp)
#include XTENSORINCLUDE(containers, xarray.hpp)
#include XTENSORINCLUDE(views, xaxis_slice_iterator.hpp)
#include XTENSORINCLUDE(io, xio.hpp)
#include XTENSORINCLUDE(generators, xbuilder.hpp)

#include "z5/factory.hxx"
#include "z5/filesystem/handle.hxx"
#include "z5/filesystem/dataset.hxx"
#include "z5/common.hxx"
#include "z5/multiarray/xtensor_access.hxx"
#include "z5/attributes.hxx"

#include <opencv2/highgui.hpp>


// std::ostream& operator<< (std::ostream& out, const std::vector<size_t> &v) {
//   if ( !v.empty() ) {
//     out << '[';
//     for(auto &v : v)
//       out << v << ",";
//     out << "\b]"; // use ANSI backspace character '\b' to overwrite final ", "
//   }
//   return out;
// }

template <size_t N>
std::ostream& operator<< (std::ostream& out, const std::array<size_t,N> &v) {
  if ( !v.empty() ) {
    out << '[';
    for(auto &v : v)
      out << v << ",";
    out << "\b]"; // use ANSI backspace character '\b' to overwrite final ", "
  }
  return out;
}

std::ostream& operator<< (std::ostream& out, const xt::svector<size_t> &v) {
  if ( !v.empty() ) {
    out << '[';
    for(auto &v : v)
      out << v << ",";
    out << "\b]"; // use ANSI backspace character '\b' to overwrite final ", "
  }
  return out;
}


void throw_run_path(const fs::path &path, const std::string msg)
{
  throw std::runtime_error(msg + " for " + path.string());
}

cv::Mat read_jxl(const fs::path &path)
{
  //adapted from https://github.com/libjxl/libjxl/blob/main/examples/decode_oneshot.cc

  std::ifstream file(path, std::ios::binary | std::ios::ate);
  std::streamsize file_size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<char> data(file_size);
  if (!file.read(data.data(), file_size))
    throw_run_path(path, "read error");


  size_t w, h;
  cv::Mat_<uint8_t> img;

  // Multi-threaded parallel runner.
  auto runner = JxlResizableParallelRunnerMake(nullptr);
  auto dec = JxlDecoderMake(nullptr);

  if (JXL_DEC_SUCCESS != JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING | JXL_DEC_FULL_IMAGE))
    throw_run_path(path, "JxlDecoderSubscribeEvents Error");

  if (JXL_DEC_SUCCESS != JxlDecoderSetParallelRunner(dec.get(), JxlResizableParallelRunner, runner.get()))
    throw_run_path(path, "JxlDecoderSetParallelRunner failed");

  JxlBasicInfo info;
  JxlPixelFormat format = {1, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
  //FIXME check format in the file and keep that?

  JxlDecoderSetInput(dec.get(), (uint8_t*)&data[0], file_size);
  JxlDecoderCloseInput(dec.get());

  for (;;) {
    JxlDecoderStatus status = JxlDecoderProcessInput(dec.get());

    if (status == JXL_DEC_ERROR)
      throw_run_path(path, "Decoder error\n");
    else if (status == JXL_DEC_NEED_MORE_INPUT)
      throw_run_path(path, "Error, already provided all input");
    else if (status == JXL_DEC_BASIC_INFO) {
      if (JXL_DEC_SUCCESS != JxlDecoderGetBasicInfo(dec.get(), &info))
        throw_run_path(path, "JxlDecoderGetBasicInfo failed");
      w = info.xsize;
      h = info.ysize;
      JxlResizableParallelRunnerSetThreads(runner.get(), JxlResizableParallelRunnerSuggestThreads(info.xsize, info.ysize));
    } else if (status == JXL_DEC_COLOR_ENCODING) {
      std::cout << "Ignoring jxl ICC profile" << "\n";
    } else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
      size_t buffer_size;
      if (JXL_DEC_SUCCESS != JxlDecoderImageOutBufferSize(dec.get(), &format, &buffer_size))
        throw_run_path(path, "JxlDecoderImageOutBufferSize failed");

      if (buffer_size != w * h)
        throw_run_path(path, "Invalid Buffer size");

      img.create(h, w);
      void* pixels_buffer = static_cast<void*>(img.ptr(0));
      if (JXL_DEC_SUCCESS != JxlDecoderSetImageOutBuffer(dec.get(), &format, pixels_buffer, buffer_size))
        throw_run_path(path, "JxlDecoderSetImageOutBuffer failed\n");

    } else if (status == JXL_DEC_FULL_IMAGE) {
      // Nothing to do. Do not yet return. If the image is an animation, more
      // full frames may be decoded. This example only keeps the last one.
    } else if (status == JXL_DEC_SUCCESS) {
      // All decoding successfully finished.
      // It's not required to call JxlDecoderReleaseInput(dec.get()) here since
      // the decoder will be destroyed.
      return img;
    } else {
      std::runtime_error("Unknown decoder Error");
    }
  }
}

int main(int argc, const char *argv[])
{
  assert(argc == 3);

  const char *src = argv[1];
  z5::filesystem::handle::File f(argv[2]);

  const bool createAsZarr = true;
  z5::createFile(f, createAsZarr);


  nlohmann::json comp_options = {
    {"blocksize", 0},
    {"level", 9},
    {"codec", "zstd"},
    {"shuffle", 2}
  };
  
  // create a new zarr dataset
  const std::string dsName = "s1";
  std::vector<size_t> shape = {7188, 3944, 4048};
  size_t chunksize = 128;
  std::vector<size_t> chunks_shape = { chunksize, chunksize, chunksize };
  auto ds = z5::createDataset(f, dsName, "uint8", shape, chunks_shape, "blosc", comp_options);

  xt::xarray<uint8_t> imgbuf = xt::ones<uint8_t>({128, 3944, 4048});

  for(int z=0;z<7188;z+=chunksize) {
    for(int i=0;i<chunksize;i++) {
      if (z+i >= 7188)
        break;
      
      char buf[256];
      snprintf(buf, 256, "%s/%05d.jxl", src, 2*i);

      std::cout << buf << std::endl;
      // cv::Mat img = cv::imread(buf);
      cv::Mat img = read_jxl(buf);
      std::cout << img.size() << "x" << img.channels() << std::endl;

      auto slice = xt::adapt((uint8_t*)img.data, std::vector<std::size_t>({img.size().height, img.size().width}));

      auto view = xt::view(imgbuf, i);
      view = slice;
      // std::cout << imgbuf.shape() << " sub " << slice.shape() << " view " << view.shape() << std::endl;
    }
    
    for(size_t y = 0;y<imgbuf.shape(1);y+=chunksize)
      for(size_t x = 0;x<imgbuf.shape(2);x+=chunksize) {
        z5::types::ShapeType offset = { z, y, x };
        // xt::xarray<uint8_t>::shape_type chunk_shape = { 128, 128, 128 };
        std::cout << "diview " << std::endl;
        auto view = xt::strided_view(imgbuf, {xt::range(z,std::min(z+128,7188)), xt::range(y,y+128), xt::range(x,x+128)});
        std::cout << "write out " << view.shape() << offset << imgbuf.shape() << std::endl;
        //FIXME why do partial chunks not work? maybe we need to always write a full chunk?! https://github.com/zarr-developers/zarr-specs/issues/44
        if (view.shape()[0] != chunksize)
          continue;
        if (view.shape()[1] != chunksize)
          continue;
        if (view.shape()[2] != chunksize)
          continue;
        z5::multiarray::writeSubarray<uint8_t>(ds, view, offset.begin());
      }
      break;
  }

  // write array to roi
//   z5::types::ShapeType offset1 = { 50, 100, 150 };
//   xt::xarray<float>::shape_type shape1 = { 150, 200, 100 };
//   xt::xarray<float> array1(shape1, 42.0);
//   z5::multiarray::writeSubarray<float>(ds, array1, offset1.begin());
//
//   // read array from roi (values that were not written before are filled with a fill-value)
//   z5::types::ShapeType offset2 = { 100, 100, 100 };
//   xt::xarray<float>::shape_type shape2 = { 300, 200, 75 };
//   xt::xarray<float> array2(shape2);
//   z5::multiarray::readSubarray<float>(ds, array2, offset2.begin());
//
//   // get handle for the dataset
//   const auto dsHandle = z5::filesystem::handle::Dataset(f, dsName);
//
//   // read and write json attributes
//   nlohmann::json attributesIn;
//   attributesIn["bar"] = "foo";
//   attributesIn["pi"] = 3.141593;
//   z5::writeAttributes(dsHandle, attributesIn);
//
//   nlohmann::json attributesOut;
//   z5::readAttributes(dsHandle, attributesOut);
//
  return 0;
}
