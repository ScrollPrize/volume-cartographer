#!/bin/bash
set -e

# Update and install system packages
sudo apt -y update
sudo apt -y install software-properties-common
sudo add-apt-repository -y universe
sudo apt -y update
sudo apt -y upgrade
sudo apt -y full-upgrade
sudo apt -y install build-essential git cmake qt6-base-dev libboost-system-dev \
    libboost-program-options-dev libceres-dev xtensor-dev libopencv-dev \
    libxsimd-dev libblosc-dev libspdlog-dev libgsl-dev libsdl2-dev \
    libcurl4-openssl-dev file curl unzip ca-certificates bzip2 wget \
    fuse jq gimp desktop-file-utils ninja-build


# Install AWS CLI
if [ "$ARCH" = "x86_64" ]; then
    curl "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip"
elif [ "$ARCH" = "aarch64" ]; then
    curl "https://awscli.amazonaws.com/awscli-exe-linux-aarch64.zip" -o "awscliv2.zip"
else
    echo "Unsupported architecture: $ARCH"
    exit 1
fi

unzip awscliv2.zip
sudo ./aws/install
rm -rf awscliv2.zip aws

# Install GL/GLib packages for Open3D (x86_64 only)
if [ "$(uname -m)" = "x86_64" ]; then
    sudo apt -y install libgl1 libglib2.0-0 libx11-6 libxext6 libxrender1 libsm6 libegl1
fi

# Clean up
sudo apt -y autoremove

echo "System setup complete!"