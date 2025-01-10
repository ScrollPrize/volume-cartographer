FROM ubuntu:jammy

RUN apt-get update
RUN apt-get upgrade
RUN apt -y install build-essential git cmake
RUN apt-get -y install software-properties-common
RUN add-apt-repository universe
RUN apt-get -y install qt6-base-dev
RUN apt-get -y install libgl1-mesa-dev libglvnd-dev

ENV TZ=Europe/Berlin
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone
RUN apt install -y tzdata

RUN apt-get -y install libceres-dev libboost-system-dev libboost-program-options-dev xtensor-dev libopencv-dev
RUN apt-get -y install libblosc-dev libspdlog-dev libsdl2-dev
RUN apt-get -y install libgsl-dev
RUN apt-get -y install file

COPY . /src

RUN mkdir /build
WORKDIR /build

RUN cmake -DVC_BUILD_JSON=on -DVC_WITH_CUDA_SPARSE=off /src
RUN make -j$(nproc --all)
#RUN make install

RUN cpack -G DEB -V

RUN dpkg -i /build/pkgs/vc3d*.deb

RUN apt-get -y autoremove
RUN rm -r /src