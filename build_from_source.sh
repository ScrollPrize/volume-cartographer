#!/bin/bash

set -e

export CC="ccache clang"
export CXX="ccache clang++"
export INSTALL_PREFIX="$HOME/ceres-install"
export BUILD_DIR="$HOME/ceres-build"
export JOBS=$(nproc)
export COMMON_FLAGS="-march=native  -w "
export COMMON_LDFLAGS="-fuse-ld=lld "

sudo apt-get update
sudo apt-get install -y libgmp-dev libmpfr-dev ccache ninja-build libc++-dev libc++abi-dev libunwind-20-dev lld \
    libcurl4-openssl-dev libopencv-dev libboost-system-dev libboost-program-options-dev

rm -rf "$BUILD_DIR" "$INSTALL_PREFIX"
mkdir -p "$BUILD_DIR" "$INSTALL_PREFIX"
cd "$BUILD_DIR"

rm -rf json
git clone --depth 1 https://github.com/nlohmann/json.git
cd json
mkdir -p build && cd build
cmake .. -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_C_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${COMMON_FLAGS}" \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_EXE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_SHARED_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_MODULE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DJSON_BuildTests=OFF
  ninja -j$JOBS
ninja install
cd "$BUILD_DIR"

rm -rf xtl
git clone --depth 1 https://github.com/xtensor-stack/xtl.git
cd xtl
mkdir -p build && cd build
cmake .. -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_C_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_SHARED_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_MODULE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DBUILD_TESTS=OFF
ninja -j$JOBS
ninja install
cd "$BUILD_DIR"

rm -rf xtensor
git clone --depth 1 https://github.com/xtensor-stack/xtensor.git
cd xtensor
mkdir -p build && cd build
cmake .. -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_C_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_SHARED_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_MODULE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DBUILD_TESTS=OFF \
    -DXTENSOR_BUILD_TESTS=OFF
ninja -j$JOBS
ninja install
cd "$BUILD_DIR"

rm -rf c-blosc
git clone --depth 1 https://github.com/Blosc/c-blosc.git
cd c-blosc
mkdir -p build && cd build
cmake .. -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_C_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_SHARED_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_MODULE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DBUILD_STATIC=ON \
    -DBUILD_TESTS=OFF \
    -DBUILD_BENCHMARKS=OFF
ninja -j$JOBS
ninja install
cd "$BUILD_DIR"

rm -rf z5
git clone --depth 1 https://github.com/SuperOptimizer/z5.git
cd z5
mkdir -p build && cd build
cmake .. -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_C_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_SHARED_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_MODULE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
    -DWITH_BLOSC=ON \
    -DWITH_ZLIB=ON \
    -DWITH_XZ=ON \
    -DWITH_LZ4=ON \
    -DBUILD_Z5PY=OFF \
    -DBUILD_TESTS=OFF

ninja -j$JOBS
ninja install
cd "$BUILD_DIR"

rm -rf spdlog
git clone --depth 1 https://github.com/gabime/spdlog.git
cd spdlog
mkdir -p build && cd build
cmake .. -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_C_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${COMMON_FLAGS}" \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_EXE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_SHARED_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_MODULE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DSPDLOG_BUILD_EXAMPLE=OFF \
    -DSPDLOG_BUILD_TESTS=OFF \
    -DSPDLOG_INSTALL=ON \
    -DSPDLOG_FMT_EXTERNAL=OFF
ninja -j$JOBS
ninja install
cd "$BUILD_DIR"

rm -rf eigen
git clone --depth 1 https://gitlab.com/libeigen/eigen.git
cd eigen
mkdir -p build && cd build
cmake .. -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_C_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_SHARED_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_MODULE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DBUILD_TESTING=OFF \
    -DEIGEN_BUILD_DOC=OFF
ninja install -j$JOBS
cd "$BUILD_DIR"

rm -rf GKlib
git clone --depth 1 https://github.com/KarypisLab/GKlib.git
cd GKlib
mkdir -p build && cd build
cmake .. -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_C_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_SHARED_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_MODULE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DGKLIB_BUILD_APPS=OFF
ninja -j$JOBS
ninja install
cd "$BUILD_DIR"

rm -rf METIS
git clone --depth 1 https://github.com/KarypisLab/METIS.git
cd METIS
# Manually do what "make config" does, but with Ninja
rm -rf build
mkdir -p build/xinclude
echo "#define IDXTYPEWIDTH 32" > build/xinclude/metis.h
echo "#define REALTYPEWIDTH 32" >> build/xinclude/metis.h
cat include/metis.h >> build/xinclude/metis.h
cp include/CMakeLists.txt build/xinclude
cd build
cmake .. -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_C_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_SHARED_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_MODULE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DGKLIB_PATH="$INSTALL_PREFIX" \
    -DGKLIB_BUILD_APPS=OFF
ninja -j$JOBS
ninja install
cd "$BUILD_DIR"

rm -rf SuiteSparse
git clone --depth 1 https://github.com/DrTimothyAldenDavis/SuiteSparse.git
cd SuiteSparse
mkdir -p build && cd build
cmake .. -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_C_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_SHARED_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_MODULE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DBLA_VENDOR=Generic \
    -DSUITESPARSE_USE_CUDA=OFF \
    -DSUITESPARSE_USE_OPENMP=OFF \
    -DSUITESPARSE_ENABLE_PROJECTS="amd;btf;camd;ccolamd;cholmod;colamd;klu;ldl;spqr;umfpack;suitesparse_config" \
    -DSUITESPARSE_DEMOS=OFF \
    -DBUILD_STATIC_LIBS=ON
ninja -j$JOBS
ninja install
cd "$BUILD_DIR"

rm -rf OpenBLAS
git clone --depth 1 https://github.com/OpenMathLib/OpenBLAS.git
cd OpenBLAS
mkdir -p build && cd build
cmake .. -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_C_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_SHARED_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_MODULE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DBUILD_STATIC_LIBS=ON \
    -DUSE_OPENMP=OFF \
    -DNOFORTRAN=ON \
    -DBUILD_TESTING=OFF \
    -DBUILD_WITHOUT_LAPACK=OFF
ninja -j$JOBS
ninja install
cd "$BUILD_DIR"

rm -rf opencv
git clone --depth 1 https://github.com/opencv/opencv.git
cd opencv
mkdir -p build && cd build
cmake .. -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_C_FLAGS="${COMMON_FLAGS}" \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_CXX_FLAGS="${COMMON_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_SHARED_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_MODULE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_opencv_apps=OFF \
    -DBUILD_opencv_python2=OFF \
    -DBUILD_opencv_python3=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_PERF_TESTS=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_DOCS=OFF \
    -DWITH_CUDA=OFF \
    -DWITH_OPENMP=OFF \
    -DWITH_TBB=OFF \
    -DWITH_IPP=OFF \
    -DWITH_VTK=OFF \
    -DWITH_FFMPEG=ON \
    -DWITH_GSTREAMER=OFF \
    -DWITH_V4L=ON \
    -DWITH_QT=OFF \
    -DWITH_GTK=OFF \
    -DWITH_OPENCL=ON \
    -DWITH_OPENEXR=OFF \
    -DOPENCV_ENABLE_NONFREE=ON
ninja -j$JOBS
ninja install
cd "$BUILD_DIR"

rm -rf ceres-solver
git clone --depth 1 --recurse-submodules --shallow-submodules https://github.com/ceres-solver/ceres-solver.git
cd ceres-solver
mkdir -p build && cd build

cmake .. -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_C_FLAGS="${COMMON_FLAGS} -g0" \
    -DCMAKE_CXX_FLAGS="${COMMON_FLAGS} -g0" \
    -DCMAKE_EXE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_SHARED_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DCMAKE_MODULE_LINKER_FLAGS="${COMMON_LDFLAGS}" \
    -DBUILD_TESTING=OFF \
    -DEIGEN_BUILD_DOC=OFF \
    -DSUITESPARSE=ON \
    -DLAPACK=ON \
    -DEIGENSPARSE=ON \
    -DEIGENMETIS=ON \
    -DSCHUR_SPECIALIZATIONS=ON \
    -DCUSTOM_BLAS=ON \
    -DUSE_CUDA=OFF \
    -DEigen3_DIR="$INSTALL_PREFIX/share/eigen3/cmake" \
    -DMETIS_LIBRARY="$INSTALL_PREFIX/lib/libmetis.a" \
    -DMETIS_INCLUDE_DIR="$INSTALL_PREFIX/include" \
    -DBLAS_LIBRARIES="$INSTALL_PREFIX/lib/libopenblas.a" \
    -DLAPACK_LIBRARIES="$INSTALL_PREFIX/lib/libopenblas.a"

ninja -j$JOBS
ninja install