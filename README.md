<!---

Copyright (C) 2018-2021 Intel Corporation

SPDX-License-Identifier: MIT

-->

# Unoffical Intel(R) Graphics Compute Runtime for oneAPI Level Zero and OpenCL(TM) Driver with support for OpenCL/OpenGL interoperability

## Introduction

The Intel(R) Graphics Compute Runtime for oneAPI Level Zero and OpenCL(TM) Driver
is an open source project providing compute API support (Level Zero, OpenCL)
for Intel graphics hardware architectures (HD Graphics, Xe). This repo is an **unoffical fork** with support for **OpenCL/OpenGL interoperability**

For more information on the offical project refer to: https://github.com/intel/compute-runtime/#readme

## Supported Platforms

At the moment only Tigerlake (Gen12) is tested but it should work for all target CPUs (Gen8 - Gen12). Please provide feedback if it worked for you on another CPU than Tigerlake.

## Install

* Create temporary directory
```bash
mkdir neo
```

* Download official *.deb packages

```
cd neo
wget https://github.com/intel/intel-graphics-compiler/releases/download/igc-1.0.12260.1/intel-igc-core_1.0.12260.1_amd64.deb
wget https://github.com/intel/intel-graphics-compiler/releases/download/igc-1.0.12260.1/intel-igc-opencl_1.0.12260.1_amd64.deb
wget https://github.com/intel/compute-runtime/releases/download/22.42.24548/intel-level-zero-gpu-dbgsym_1.3.24548_amd64.ddeb
wget https://github.com/intel/compute-runtime/releases/download/22.42.24548/intel-level-zero-gpu_1.3.24548_amd64.deb
wget https://github.com/intel/compute-runtime/releases/download/22.42.24548/intel-opencl-icd-dbgsym_22.42.24548_amd64.ddeb
wget https://github.com/intel/compute-runtime/releases/download/22.42.24548/intel-opencl-icd_22.42.24548_amd64.deb
wget https://github.com/intel/compute-runtime/releases/download/22.42.24548/libigdgmm12_22.2.0_amd64.deb
```

* Download unofficial *.deb packages
