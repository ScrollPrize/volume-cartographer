FROM ubuntu:plucky as base

RUN apt-get update
RUN apt-get -y upgrade
RUN apt-get -y install software-properties-common
RUN add-apt-repository universe
RUN apt-get update
RUN apt-get -y install build-essential git cmake
RUN apt-get -y install qt6-base-dev libboost-system-dev libboost-program-options-dev
RUN apt-get -y install libceres-dev xtensor-dev libopencv-dev libxsimd-dev libblosc-dev libspdlog-dev
RUN apt-get -y install libgsl-dev libsdl2-dev libcurl4-openssl-dev
RUN apt-get -y install file
RUN apt-get -y install awscli

COPY . /src
RUN rm /src/CMakeCache.txt || true

RUN ls /src
RUN mkdir /src/build
WORKDIR /src/build

RUN cmake -DVC_WITH_CUDA_SPARSE=off /src
RUN make -j$(nproc --all)

RUN cpack -G DEB -V

RUN dpkg -i /src/build/pkgs/vc3d*.deb

FROM base as gimp
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y install \
    gimp desktop-file-utils \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

FROM base as default
