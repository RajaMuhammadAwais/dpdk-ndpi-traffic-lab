#!/bin/bash
 
# Exit immediately if a command exits with a non-zero status.
set -e

echo "Starting DPDK + nDPI Lab Setup Script"

# 1. Environment Preparation
echo "1. Installing build dependencies..."
sudo apt update
sudo apt install -y build-essential meson ninja-build pkg-config \
    libnuma-dev python3-pyelftools libpcap-dev libtool \
    autoconf automake git

# 2. DPDK Installation (v23.11)
echo "2. Installing DPDK v23.11..."
DPDK_DIR="/home/ubuntu/dpdk"
if [ -d "$DPDK_DIR" ]; then
    echo "DPDK directory already exists. Removing and re-cloning."
    sudo rm -rf "$DPDK_DIR"
fi
git clone --depth 1 --branch v23.11 https://github.com/DPDK/dpdk.git "$DPDK_DIR"
cd "$DPDK_DIR"
meson setup build
ninja -C build
sudo ninja -C build install
sudo ldconfig

# Create symlink for nDPI build if needed (old nDPI Makefiles might look for it)
if [ ! -L "/home/ubuntu/DPDK" ]; then
    echo "Creating DPDK symlink for nDPI compatibility."
    ln -s "$DPDK_DIR" /home/ubuntu/DPDK
fi

# 3. nDPI Installation
echo "3. Installing nDPI..."
NDPI_DIR="/home/ubuntu/ndpi"
if [ -d "$NDPI_DIR" ]; then
    echo "nDPI directory already exists. Removing and re-cloning."
    sudo rm -rf "$NDPI_DIR"
fi
git clone https://github.com/ntop/nDPI.git "$NDPI_DIR"
cd "$NDPI_DIR"
./autogen.sh
./configure
make
sudo make install
sudo ldconfig

# 4. Compile the Lab Application
echo "4. Compiling the DPDK + nDPI lab application..."
cd /home/ubuntu/dpdk_ndpi_lab
make

echo "Setup complete! You can now run the application from /home/ubuntu/dpdk_ndpi_lab/main"
echo "Example usage: sudo ./main -l 0 --vdev=net_pcap0,iface=eth0 --no-huge --file-prefix=lab1"
