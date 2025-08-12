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
RUN apt-get -y install file curl unzip

RUN ARCH=$(uname -m) && \
    if [ "$ARCH" = "x86_64" ]; then \
        curl "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip"; \
    elif [ "$ARCH" = "aarch64" ]; then \
        curl "https://awscli.amazonaws.com/awscli-exe-linux-aarch64.zip" -o "awscliv2.zip"; \
    else \
        echo "Unsupported architecture: $ARCH" && exit 1; \
    fi && \
    unzip awscliv2.zip && \
    ./aws/install && \
    rm -rf awscliv2.zip aws


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
