#!/bin/bash

export ARCH=arm64
export CROSS_COMPILE=/home/kali/Android/Toolchains/aarch64-xxmustafacooTR-elf/bin/aarch64-xxmustafacooTR-elf-
export CROSS_COMPILE_ARM32=/home/kali/Android/Toolchains/arm-xxmustafacooTR-eabi/bin/arm-xxmustafacooTR-eabi-
export CC=/home/kali/Android/Toolchains/proton-clang/bin/clang
export ANDROID_MAJOR_VERSION=r

echo "Write 'ulimit -n unlimited' to current terminal"
make exynos9810-star2lte_defconfig
make -j8
