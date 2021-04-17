#!/bin/bash



export ANDROID_MAJOR_VERSION=r
export ARCH=arm64
make exynos9810-crownlte_defconfig
make -j16