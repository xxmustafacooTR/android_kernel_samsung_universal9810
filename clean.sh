#!/bin/bash

rm -rf drivers/gator_5.27/gator_src_md5.h arch/arm64/boot/dts/exynos/*dtb*
make clean
make mrproper
