<!---

Copyright (C) 2018-2021 Intel Corporation

SPDX-License-Identifier: MIT

-->

# Unoffical Intel(R) Graphics Compute Runtime for oneAPI Level Zero and OpenCL(TM) Driver with limited support for OpenCL/OpenGL interoperability

## Introduction

The Intel(R) Graphics Compute Runtime for oneAPI Level Zero and OpenCL(TM) Driver
is an open source project providing compute API support (Level Zero, OpenCL)
for Intel graphics hardware architectures (HD Graphics, Xe). This repo is an **unoffical fork** with limited support for **OpenCL/OpenGL interoperability**

## Supported

Only the first device in platform.
* EGL (GLX is not supported)
* GL_TEXTURE_2D target
* GL internal_formats: GL_RGBA, GL_RGBA8, GL_RGBA16F, GL_RGB

For more information on the offical project refer to: https://github.com/intel/compute-runtime/#readme

## Supported Platforms

At the moment only Tigerlake (Gen11) is tested but it should work for all target CPUs (Gen8 - Gen12). Please provide feedback if it worked for you on another CPU than Tigerlake.

## Install
There are tarballs in every [release](https://github.com/kallaballa/compute-runtime/releases/) that contain all the files needed. Please check if the paths comply with your Linux distribution.

## Build binary tarballs

```bash
# Prepare a Ubuntu 22.04 chroot
sudo debootstrap --variant=minbase --arch=amd64 jammy jammy http://de.archive.ubuntu.com/ubuntu
sudo mount --bind /dev/ jammy/dev
# Enter the chroot
sudo chroot jammy

# From here on we are working inside the chroot

# Setup basic filesystems
mount -t proc none proc
mount -t sysfs none sys
mount -t tmpfs none tmp
mount -t devpts none /dev/pts

# Configure apt sources
echo deb http://de.archive.ubuntu.com/ubuntu jammy main universe > /etc/apt/sources.list
apt-get update

# Install required packages
apt-get install cmake g++ git pkg-config sudo wget libegl-dev python3 devscripts libigdgmm-dev debhelper ninja-build libva-dev

# Retrieve and install official prebuilt packages we need for building
wget https://github.com/intel/intel-graphics-compiler/releases/download/igc-1.0.12504.5/intel-igc-core_1.0.12504.5_amd64.deb
wget https://github.com/intel/intel-graphics-compiler/releases/download/igc-1.0.12504.5/intel-igc-opencl_1.0.12504.5_amd64.deb
wget https://github.com/intel/intel-graphics-compiler/releases/download/igc-1.0.12504.5/intel-igc-opencl-devel_1.0.12504.5_amd64.deb
dpkg -i intel-igc-core_1.0.12504.5_amd64.deb intel-igc-opencl_1.0.12504.5_amd64.deb intel-igc-opencl-devel_1.0.12504.5_amd64.deb

# Clone the unofficial compute runtime with support for OpenCL/OpenGL interop
git clone https://github.com/kallaballa/compute-runtime.git neo

# Check out the release tag
cd /neo && git checkout -b release 22.43.24595.30-clgl

# Prepare directory for binary tarball build
mkdir /neo/build && cd /neo/build

# Configure the build using cmake
cmake -DCMAKE_BUILD_TYPE=Release -DNEO_CPACK_GENERATOR=TGZ -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_INSTALL_SYSCONFDIR=/etc -DCMAKE_INSTALL_LOCALSTATEDIR=/var -DCMAKE_EXPORT_NO_PACKAGE_REGISTRY=ON -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF -DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON -DCMAKE_INSTALL_RUNSTATEDIR=/run -DCMAKE_VERBOSE_MAKEFILE=ON -DCMAKE_INSTALL_LIBDIR=lib64 -DNEO_OCL_VERSION_MAJOR=22 -DNEO_OCL_VERSION_MINOR=43 -DNEO_VERSION_BUILD=24595.30 -DDO_NOT_RUN_AUB_TESTS=FALSE -DNEO_SKIP_UNIT_TESTS=1 -DNEO_ENABLE_i915_PRELIM_DETECTION=TRUE -DNEO_DISABLE_BUILTINS_COMPILATION=FALSE -DRELEASE_WITH_REGKEYS=FALSE -DIGDRCL_FORCE_USE_LIBVA=FALSE -DNEO_SKIP_OCL_UNIT_TESTS=1 -DDISABLE_WDDM_LINUX=1 -DBUILD_WITH_L0=1 -DNEO_DISABLE_LD_GOLD=1 -Wno-dev -DCOMPILE_BUILT_INS=OFF ..

# Build the binary tarballs
make -j`nproc` package

# list the tarballs
ls *.tar.gz
```
