FROM ghcr.io/educelab/ci-docker:12.1.1

# Set Qt environment variables to ensure consistent Qt 6.7.2 usage
ENV Qt6_DIR=/usr/local/Qt-6.7.2
ENV LD_LIBRARY_PATH=/usr/local/Qt-6.7.2/lib:${LD_LIBRARY_PATH}
ENV PATH=/usr/local/Qt-6.7.2/bin:${PATH}

# The CI container has Qt6 and many other dependencies via vc-deps
# We need to install additional dependencies specific to volume-cartographer
# NOTE: We do NOT install qt6-base-dev as Qt 6.7.2 is already in the base image
RUN apt-get update && apt-get install -y \
    ninja-build \
    libceres-dev \
    libopencv-dev \
    xtensor-dev \
    libxsimd-dev \
    libblosc-dev \
    libsdl2-dev \
    libcurl4-openssl-dev \
    libboost-system-dev \
    libboost-program-options-dev \
    libgsl-dev \
    libzstd-dev \
    libtiff-dev \
    libjpeg-dev \
    liblzma-dev \
    zlib1g-dev \
    libspdlog-dev \
    libfmt-dev \
    pkg-config \
    file \
    wget \
    && apt-get clean

# # Build ITK from source since it's not included in ci-docker
# RUN cd /tmp && \
#     wget https://github.com/InsightSoftwareConsortium/ITK/archive/refs/tags/v5.3.0.tar.gz && \
#     tar -xzf v5.3.0.tar.gz && \
#     cd ITK-5.3.0 && \
#     mkdir build && \
#     cd build && \
#     cmake -DCMAKE_BUILD_TYPE=Release \
#           -DBUILD_TESTING=OFF \
#           -DBUILD_EXAMPLES=OFF \
#           -DITK_BUILD_DEFAULT_MODULES=ON \
#           -DCMAKE_INSTALL_PREFIX=/usr \
#           .. && \
#     make -j$(nproc) && \
#     make install && \
#     cd / && \
#     rm -rf /tmp/ITK*
    
COPY . /src
RUN rm /src/CMakeCache.txt || true

RUN ls /src
RUN mkdir /src/build
WORKDIR /src/build

# Configure with ACVD support using Ninja generator
# Qt6 is installed in /usr/local/Qt-6.7.2/ and vc-deps libraries are in /usr
RUN cmake -GNinja \
    -DVC_WITH_CUDA_SPARSE=off \
    -DVC_BUILD_ACVD=ON \
    -DCMAKE_PREFIX_PATH="/usr/local/Qt-6.7.2;/usr" \
    -DQt6_DIR=/usr/local/Qt-6.7.2/lib/cmake/Qt6 \
    /src

# Build with ninja
RUN ninja

# Ensure Qt libraries are found at runtime
RUN echo "/usr/local/Qt-6.7.2/lib" > /etc/ld.so.conf.d/qt6.conf && ldconfig

# The built executables are now available in /src/build
# VC3D binary is at /src/build/apps/VC3D/VC3D
