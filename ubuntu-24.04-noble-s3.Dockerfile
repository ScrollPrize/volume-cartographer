FROM ubuntu:noble

RUN apt-get update
RUN apt-get -y upgrade
RUN apt-get -y install software-properties-common
RUN add-apt-repository universe
RUN apt-get update
RUN apt-get -y install build-essential git cmake 
RUN apt-get -y install qt6-base-dev libboost-system-dev libboost-program-options-dev
RUN apt-get update
RUN apt-get -y install libceres-dev xtensor-dev libopencv-dev libxsimd-dev libblosc-dev libspdlog-dev
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

RUN apt-get -y install curl wget unzip

RUN curl "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip"
RUN unzip awscliv2.zip
RUN ./aws/install

RUN apt-get -y install fuse jq
RUN wget https://github.com/kahing/goofys/releases/latest/download/goofys -O /usr/local/bin/goofys
RUN chmod +x /usr/local/bin/goofys

COPY docker_s3_entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
