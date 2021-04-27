#!/bin/bash

export ARCH=arm64
export CROSS_COMPILE=/home/kali/Android/Toolchains/gcc-prebuilt-elf-toolchains/aarch64-milouk-elf/bin/aarch64-milouk-elf-
export CROSS_COMPILE_ARM32=/home/kali/Android/Toolchains/gcc-9.2-arm-none-eabi/bin/arm-none-eabi-
export CC=/home/kali/Android/Toolchains/proton-clang/bin/clang
export ANDROID_MAJOR_VERSION=r

make exynos9810-star2lte_defconfig
make -j8
