#!/bin/bash

#
# Copyright (C) 2018-2021 Intel Corporation
#
# SPDX-License-Identifier: MIT
#

wget https://aur.archlinux.org/cgit/aur.git/snapshot/spirv-llvm-translator.tar.gz
tar -xzf spirv-llvm-translator.tar.gz
cd spirv-llvm-translator
makepkg -i --noconfirm
cd ..

wget https://aur.archlinux.org/cgit/aur.git/snapshot/intel-opencl-clang.tar.gz
tar -xzf intel-opencl-clang.tar.gz
cd intel-opencl-clang
makepkg -i --noconfirm
cd ..

wget https://aur.archlinux.org/cgit/aur.git/snapshot/intel-graphics-compiler.tar.gz
tar -xzf intel-graphics-compiler.tar.gz
cd intel-graphics-compiler
makepkg -i --noconfirm
cd ..
