FROM ubuntu:noble

RUN apt update
RUN apt -y upgrade
RUN apt -y install software-properties-common
RUN add-apt-repository universe
RUN apt update
RUN apt -y install build-essential git cmake
RUN apt -y install qt6-base-dev libboost-system-dev libboost-program-options-dev
RUN apt update
RUN apt -y install libceres-dev xtensor-dev libopencv-dev libxsimd-dev libblosc-dev libspdlog-dev
RUN apt -y install libgsl-dev libsdl2-dev libcurl4-openssl-dev
RUN apt -y install file curl unzip

# ----- Python 3.10 env (micromamba) + Open3D runtime deps -----
# Open3D needs GL/GLib bits even for headless usage.
RUN apt update && DEBIAN_FRONTEND=noninteractive apt -y install \
        ca-certificates bzip2 \
        libgl1 libglib2.0-0 libx11-6 libxext6 libxrender1 libsm6 libegl1

# Install micromamba with architecture detection
RUN ARCH=$(uname -m) && \
    if [ "$ARCH" = "x86_64" ]; then \
        MICROMAMBA_ARCH="linux-64"; \
    elif [ "$ARCH" = "aarch64" ]; then \
        MICROMAMBA_ARCH="linux-aarch64"; \
    else \
        echo "Unsupported architecture: $ARCH" && exit 1; \
    fi && \
    curl -Ls "https://micro.mamba.pm/api/micromamba/${MICROMAMBA_ARCH}/latest" | \
    tar -xvj -C /usr/local/bin bin/micromamba --strip-components=1

# Install AWS CLI with architecture detection
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

ENV MAMBA_ROOT_PREFIX=/opt/micromamba
SHELL ["/bin/bash", "-lc"]
RUN micromamba create -y -n py310 -c conda-forge python=3.10 pip \
 && micromamba run -n py310 python -m pip install --upgrade pip
RUN micromamba run -n py310 pip install --no-cache-dir numpy==1.26.4 pillow tqdm wandb

#These packages are not available on arm64 so skip for now
#TODO: install from source?
RUN if [ "$(uname -m)" = "x86_64" ]; then \
        micromamba run -n py310 pip install --no-cache-dir libigl==2.5.1 open3d==0.18.0; \
    fi

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

RUN apt -y autoremove
RUN rm -r /src

RUN apt -y update
RUN apt -y full-upgrade
RUN apt -y install curl wget unzip fuse jq


COPY docker_s3_entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

ENV WANDB_ENTITY="vesuvius-challenge"

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
