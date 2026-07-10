#!/bin/bash

# ==============================================================================
# DPDK + nDPI Lab Setup Script
# Description: Automated installation of DPDK and nDPI for network analysis.
# Author: Open Source Contributor
# License: MIT
# ==============================================================================

set -ex


# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Check for root privileges
if [[ $EUID -ne 0 ]]; then
   log_error "This script must be run as root (use sudo)"
   exit 1
fi

# Configuration
DPDK_VERSION="v23.11"
INSTALL_DIR="/opt/dpdk_ndpi_lab"
mkdir -p "$INSTALL_DIR"

log_info "Starting environment preparation..."
apt update
apt install -y build-essential meson ninja-build pkg-config \
    libnuma-dev python3-pyelftools libpcap-dev libtool \
    autoconf automake git

# 1. DPDK Installation
log_info "Installing DPDK ${DPDK_VERSION}..."
cd "$INSTALL_DIR"
if [ -d "dpdk" ]; then
    log_warn "DPDK directory already exists. Removing and re-cloning."
    sudo rm -rf "dpdk"
fi
git clone --depth 1 --branch "$DPDK_VERSION" https://github.com/DPDK/dpdk.git
cd dpdk
sudo meson setup build
sudo ninja -C build
sudo ninja -C build install
ldconfig

# 2. nDPI Installation
log_info "Installing nDPI (latest)..."
cd "$INSTALL_DIR"
log_info "Checking for existing nDPI directory..."
if [ -d "ndpi" ]; then
    log_warn "nDPI directory already exists. Removing and re-cloning."
    sudo rm -rf "ndpi"
fi
log_info "Cloning nDPI repository..."
git clone https://github.com/ntop/nDPI.git
cd nDPI
./autogen.sh
./configure
make -j$(nproc)
sudo make install
ldconfig

# Set PKG_CONFIG_PATH for subsequent builds
export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/usr/local/lib/x86_64-linux-gnu/pkgconfig
export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/usr/local/lib/pkgconfig

# 3. Build Lab Application
log_info "Compiling the Lab Application..."
log_info "PKG_CONFIG_PATH: $PKG_CONFIG_PATH"
# Navigate to the script's directory (assuming repo files are here)
REPO_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$REPO_DIR"
make clean || true
make

log_info "Setup Complete!"
log_info "To run the application:"
echo -e "${YELLOW}sudo ./main -l 0 --vdev=net_pcap0,iface=eth0 --no-huge --file-prefix=lab1${NC}"
