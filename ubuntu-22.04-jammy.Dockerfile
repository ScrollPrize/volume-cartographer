FROM ubuntu:jammy

RUN apt-get update
RUN apt-get -y upgrade
RUN apt-get -y install software-properties-common
RUN add-apt-repository universe
RUN apt-get update
RUN apt-get -y install build-essential git cmake 
RUN apt-get -y install qt6-base-dev libgl1-mesa-dev libglvnd-dev

ENV TZ=Europe/Berlin
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone
RUN apt install -y tzdata

RUN apt-get -y install libceres-dev libboost-system-dev libboost-program-options-dev xtensor-dev libopencv-dev
RUN apt-get -y install libblosc-dev libspdlog-dev 
RUN apt-get -y install libgsl-dev libsdl2-dev libcurl4-openssl-dev
RUN apt-get -y install file

COPY . /src
RUN rm /src/CMakeCache.txt || true

RUN ls /src
RUN mkdir /src/build
WORKDIR /src/build

RUN cmake -DVC_WITH_CUDA_SPARSE=off /src
RUN make -j$(nproc --all)

RUN cpack -G DEB -V

RUN dpkg -i /src/build/pkgs/vc3d*.deb

RUN apt-get -y autoremove
RUN rm -r /src