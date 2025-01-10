FROM ubuntu:oracular

RUN apt-get update
RUN apt-get -y upgrade
RUN apt-get -y install software-properties-common
RUN add-apt-repository universe
RUN apt-get update
RUN apt-get -y install build-essential git cmake qt6-base-dev libceres-dev libboost-system-dev libboost-program-options-dev xtensor-dev libopencv-dev libxsimd-dev libblosc-dev libspdlog-dev libgsl-dev libsdl2-dev

COPY . /src

RUN mkdir /build
WORKDIR /build

RUN cmake -DVC_WITH_CUDA_SPARSE=off /src
RUN make -j$(nproc --all)
RUN make install