FROM ubuntu:noble

RUN apt-get update
RUN apt-get -y upgrade
RUN apt-get -y install software-properties-common
RUN add-apt-repository universe
RUN apt-get update
RUN apt -y install build-essential git cmake
RUN apt-get -y install qt6-base-dev
RUN apt-get -y install libceres-dev libboost-system-dev libboost-program-options-dev xtensor-dev libopencv-dev
RUN apt-get -y install libxsimd-dev
RUN apt-get -y install libblosc-dev libspdlog-dev
RUN apt-get -y install libgsl-dev libsdl2-dev

COPY . /src

RUN mkdir /build
WORKDIR /build

RUN cmake -DVC_WITH_CUDA_SPARSE=off /src
RUN make -j$(nproc --all)
#RUN make install

RUN apt-get -y install file

RUN cpack -G DEB -V

RUN dpkg -i /build/pkgs/vc3d*.deb

RUN apt-get -y autoremove
RUN rm -r /src
