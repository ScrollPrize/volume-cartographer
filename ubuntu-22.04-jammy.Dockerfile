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
RUN apt-get -y install file curl unzip

# ----- Python 3.10 env (micromamba) + Open3D runtime deps -----
# Open3D needs GL/GLib bits even for headless usage.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y install \
        ca-certificates bzip2 \
        libgl1 libglib2.0-0 libx11-6 libxext6 libxrender1 libsm6 libegl1 \
     && rm -rf /var/lib/apt/lists/*
    
# Install micromamba (tiny conda) to get a clean Python 3.10
RUN curl -Ls https://micro.mamba.pm/api/micromamba/linux-64/latest | tar -xvj -C /usr/local/bin bin/micromamba --strip-components=1

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

 # Create Python 3.10 env and install your packages via pip
ENV MAMBA_ROOT_PREFIX=/opt/micromamba
SHELL ["/bin/bash", "-lc"]
RUN micromamba create -y -n py310 -c conda-forge python=3.10 pip \
 && micromamba run -n py310 python -m pip install --upgrade pip \
 && micromamba run -n py310 pip install --no-cache-dir \
      numpy==1.26.4 \
      pillow \
      tqdm \
      libigl==2.5.1 \
      open3d==0.18.0 \
      wandb

# Make this Python visible to subsequent steps
ENV PATH="/opt/micromamba/envs/py310/bin:${PATH}"

COPY . /src
RUN rm /src/CMakeCache.txt || true

RUN ls /src
RUN mkdir /src/build
WORKDIR /src/build

RUN cmake -DVC_WITH_CUDA_SPARSE=off /src
RUN make -j"$(nproc --all)"

RUN cpack -G DEB -V

RUN dpkg -i /src/build/pkgs/vc3d*.deb

RUN apt-get -y autoremove
RUN rm -r /src

ENV WANDB_ENTITY="vesuvius-challenge"