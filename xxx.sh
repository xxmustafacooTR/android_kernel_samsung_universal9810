#!/bin/bash

# Bash Color
green='\033[01;32m'
red='\033[01;31m'
blink_red='\033[05;31m'
restore='\033[0m'



# Resources
THREAD="-j$(grep -c ^processor /proc/cpuinfo)"
KERNEL="Image"
DTBIMAGE="dtb.img"

# Defconfigs
STARSDEFCONFIG="exynos9810-starlte_defconfig_stock"
STAR2SDEFCONFIG="exynos9810-star2lte_defconfig_stock"
CROWNSDEFCONFIG="exynos9810-crownlte_defconfig_stock"
STARBDEFCONFIG="exynos9810-starlte_defconfig_battery"
STAR2BDEFCONFIG="exynos9810-star2lte_defconfig_battery"
CROWNBDEFCONFIG="exynos9810-crownlte_defconfig_battery"
STARDEFCONFIG="exynos9810-starlte_defconfig"
STAR2DEFCONFIG="exynos9810-star2lte_defconfig"
CROWNDEFCONFIG="exynos9810-crownlte_defconfig"

# Build dirs
KERNEL_DIR="/home/kali/Android/Kernel/android_kernel_samsung_universal9810"
RESOURCE_DIR="$KERNEL_DIR/.."
KERNELFLASHER_DIR="$KERNEL_DIR/KernelFlasher"
if [ ! -d $KERNELFLASHER_DIR ]
then
     mkdir $KERNELFLASHER_DIR
fi;
KERNELFLASHERKERNEL_DIR="$KERNELFLASHER_DIR/Kernel"
if [ ! -d $KERNELFLASHERKERNEL_DIR ]
then
     mkdir $KERNELFLASHERKERNEL_DIR
fi;
TOOLCHAINS_DIRECTORY=/home/kali/Android/Toolchains/
TOOLCHAIN_DIR="$TOOLCHAINS_DIRECTORY"

# Kernel Details
BASE_YARPIIN_VER="xxmustafacooTR"
VER="-040-AROMA"
YARPIIN_VER="$BASE_YARPIIN_VER$VER"
STAR_VER=""
STAR2_VER=""
CROWN_VER=""

# Vars
export ARCH=arm64
export SUBARCH=arm64
export KBUILD_BUILD_USER=kali
export KBUILD_BUILD_HOST=kernel


# Image dirs
ZIP_MOVE="/home/kali/Android/Kernel/Zip"
ZIMAGE_DIR="$KERNEL_DIR/arch/arm64/boot"

	export CROSS_COMPILE="ccache "$TOOLCHAINS_DIRECTORY"gcc-prebuilt-elf-toolchains-master/aarch64-linux-elf/bin/aarch64-linux-elf-"
	export PLATFORM_VERSION=10.0.0
	export ANDROID_MAJOR_VERSION=q
	export CROSS_COMPILE_ARM32="ccache "$TOOLCHAINS_DIRECTORY"gcc-9.2-arm-none-eabi/bin/arm-none-eabi-"
	export LDLLD="ccache "$TOOLCHAINS_DIRECTORY"proton-clang/bin/ld.lld"
	export CC="ccache "$TOOLCHAINS_DIRECTORY"proton-clang/bin/clang"
	export AR="ccache "$TOOLCHAINS_DIRECTORY"proton-clang/bin/llvm-ar"
	export NM="ccache "$TOOLCHAINS_DIRECTORY"proton-clang/bin/llvm-nm"
	export OBJCOPY="ccache "$TOOLCHAINS_DIRECTORY"proton-clang/bin/llvm-objcopy"
	export OBJDUMP="ccache "$TOOLCHAINS_DIRECTORY"proton-clang/bin/llvm-objdump"
	export STRIP="ccache "$TOOLCHAINS_DIRECTORY"proton-clang/bin/llvm-strip"
	export CLANG_TRIPLE="ccache "$TOOLCHAINS_DIRECTORY"cross-pi-gcc-10.2.0-64/bin/aarch64-linux-gnu-"

# Functions
function clean_all {
		if [ -f "$MODULES_DIR/*.ko" ]; then
			rm `echo $MODULES_DIR"/*.ko"`
		fi
		cd $KERNEL_DIR
		echo
		# rm -f arch/arm64/configs/exynos9810_defconfig
		rm -f arch/arm64/boot/dtb.img arch/arm64/boot/dts/exynos/exynos9810-star2lte_eur_open_26.dtb arch/arm64/boot/dts/exynos/exynos9810-star2lte_eur_open_26.dtb.reverse.dts arch/arm64/boot/dts/exynos/exynos9810-starlte_eur_open_26.dtb arch/arm64/boot/dts/exynos/exynos9810-starlte_eur_open_26.dtb.reverse.dts arch/arm64/boot/dts/exynos/exynos9810-crownlte_eur_open_26.dtb arch/arm64/boot/dts/exynos/exynos9810-crownlte_eur_open_26.dtb.reverse.dts $KERNEL_DIR/arch/arm64/configs/$STAR2DEFCONFIG $KERNEL_DIR/arch/arm64/configs/$STARDEFCONFIG $KERNEL_DIR/arch/arm64/configs/$CROWNDEFCONFIG
		make -j$(nproc) clean
		make -j$(nproc) mrproper
        	rm -rf out/
}

function make_star2_kernel {
	echo
	cp -vr $KERNEL_DIR/arch/arm64/configs/$STAR2SDEFCONFIG $KERNEL_DIR/arch/arm64/configs/$STAR2DEFCONFIG
        make ARCH=arm64 $STAR2DEFCONFIG
	# cp -vr $KERNEL_DIR/arch/arm64/configs/$STAR2DEFCONFIG $KERNEL_DIR/arch/arm64/configs/exynos9810_defconfig
	make -j$(nproc --all)

	cp -vr $ZIMAGE_DIR/$KERNEL $KERNELFLASHER_DIR/Kernel/G965zImage
        cp -vr $ZIMAGE_DIR/$DTBIMAGE $KERNELFLASHER_DIR/Kernel/G965dtb.img
}

function make_star_kernel {
	echo
	cp -vr $KERNEL_DIR/arch/arm64/configs/$STARSDEFCONFIG $KERNEL_DIR/arch/arm64/configs/$STARDEFCONFIG
	make ARCH=arm64 $STARDEFCONFIG
	rm -f $ZIMAGE_DIR/$KERNEL $KERNELFLASHER_DIR/Kernel/G960zImage.diff $ZIMAGE_DIR/$DTBIMAGE $KERNELFLASHER_DIR/Kernel/G960dtb.diff
	# cp -vr $KERNEL_DIR/arch/arm64/configs/$STARDEFCONFIG $KERNEL_DIR/arch/arm64/configs/exynos9810_defconfig
	make -j$(nproc --all)

	bsdiff $KERNELFLASHER_DIR/Kernel/G965zImage $ZIMAGE_DIR/$KERNEL $KERNELFLASHER_DIR/Kernel/G960zImage.diff
	bsdiff $KERNELFLASHER_DIR/Kernel/G965dtb.img $ZIMAGE_DIR/$DTBIMAGE $KERNELFLASHER_DIR/Kernel/G960dtb.diff
	# cp -vr $ZIMAGE_DIR/$KERNEL $KERNELFLASHER_DIR/Kernel/G960/zImage
        # cp -vr $ZIMAGE_DIR/$DTBIMAGE $KERNELFLASHER_DIR/Kernel/G960/dtb.img
}

function make_crown_kernel {
	echo
	cp -vr $KERNEL_DIR/arch/arm64/configs/$CROWNSDEFCONFIG $KERNEL_DIR/arch/arm64/configs/$CROWNDEFCONFIG
        make ARCH=arm64 $CROWNDEFCONFIG
	rm -f $ZIMAGE_DIR/$KERNEL $KERNELFLASHER_DIR/Kernel/N960zImage.diff $ZIMAGE_DIR/$DTBIMAGE $KERNELFLASHER_DIR/Kernel/N960dtb.diff
	# cp -vr $KERNEL_DIR/arch/arm64/configs/$CROWNDEFCONFIG $KERNEL_DIR/arch/arm64/configs/exynos9810_defconfig
	make -j$(nproc --all)

	bsdiff $KERNELFLASHER_DIR/Kernel/G965zImage $ZIMAGE_DIR/$KERNEL $KERNELFLASHER_DIR/Kernel/N960zImage.diff
	bsdiff $KERNELFLASHER_DIR/Kernel/G965dtb.img $ZIMAGE_DIR/$DTBIMAGE $KERNELFLASHER_DIR/Kernel/N960dtb.diff
	# cp -vr $ZIMAGE_DIR/$KERNEL $KERNELFLASHER_DIR/Kernel/N960/zImage
        # cp -vr $ZIMAGE_DIR/$DTBIMAGE $KERNELFLASHER_DIR/Kernel/N960/dtb.img
}

function make_star2b_kernel {
	echo
	cp -vr $KERNEL_DIR/arch/arm64/configs/$STAR2BDEFCONFIG $KERNEL_DIR/arch/arm64/configs/$STAR2DEFCONFIG
        make ARCH=arm64 $STAR2DEFCONFIG
	rm -f $ZIMAGE_DIR/$KERNEL $KERNELFLASHER_DIR/Kernel/G965BzImage.diff $ZIMAGE_DIR/$DTBIMAGE $KERNELFLASHER_DIR/Kernel/G965dtb.diff
	# cp -vr $KERNEL_DIR/arch/arm64/configs/$STAR2DEFCONFIG $KERNEL_DIR/arch/arm64/configs/exynos9810_defconfig
	make -j$(nproc --all)

	bsdiff $KERNELFLASHER_DIR/Kernel/G965zImage $ZIMAGE_DIR/$KERNEL $KERNELFLASHER_DIR/Kernel/G965BzImage.diff
	cp -vr $ZIMAGE_DIR/$DTBIMAGE $KERNELFLASHER_DIR/Kernel/G965dtb.img
}

function make_starb_kernel {
	echo
	cp -vr $KERNEL_DIR/arch/arm64/configs/$STARBDEFCONFIG $KERNEL_DIR/arch/arm64/configs/$STARDEFCONFIG
        make ARCH=arm64 $STARDEFCONFIG
	rm -f $ZIMAGE_DIR/$KERNEL $KERNELFLASHER_DIR/Kernel/G960BzImage.diff $ZIMAGE_DIR/$DTBIMAGE $KERNELFLASHER_DIR/Kernel/G960dtb.diff
	# cp -vr $KERNEL_DIR/arch/arm64/configs/$STARDEFCONFIG $KERNEL_DIR/arch/arm64/configs/exynos9810_defconfig
	make -j$(nproc --all)

	bsdiff $KERNELFLASHER_DIR/Kernel/G965zImage $ZIMAGE_DIR/$KERNEL $KERNELFLASHER_DIR/Kernel/G960BzImage.diff
	bsdiff $KERNELFLASHER_DIR/Kernel/G965dtb.img $ZIMAGE_DIR/$DTBIMAGE $KERNELFLASHER_DIR/Kernel/G960dtb.diff
}

function make_crownb_kernel {
	echo
	cp -vr $KERNEL_DIR/arch/arm64/configs/$CROWNBDEFCONFIG $KERNEL_DIR/arch/arm64/configs/$CROWNDEFCONFIG
        make ARCH=arm64 $CROWNDEFCONFIG
	rm -f $ZIMAGE_DIR/$KERNEL $KERNELFLASHER_DIR/Kernel/N960BzImage.diff $ZIMAGE_DIR/$DTBIMAGE $KERNELFLASHER_DIR/Kernel/N960dtb.diff
	# cp -vr $KERNEL_DIR/arch/arm64/configs/$CROWNDEFCONFIG $KERNEL_DIR/arch/arm64/configs/exynos9810_defconfig
	make -j$(nproc --all)

	bsdiff $KERNELFLASHER_DIR/Kernel/G965zImage $ZIMAGE_DIR/$KERNEL $KERNELFLASHER_DIR/Kernel/N960BzImage.diff
	bsdiff $KERNELFLASHER_DIR/Kernel/G965dtb.img $ZIMAGE_DIR/$DTBIMAGE $KERNELFLASHER_DIR/Kernel/N960dtb.diff
}

function make_zip {
		cd $KERNELFLASHER_DIR
		zip -r9 `echo $YARPIIN_VER`.zip *
		mv  `echo $YARPIIN_VER`.zip $ZIP_MOVE
		cd $KERNEL_DIR
}
DATE_START=$(date +"%s")

echo -e "${green}"
echo "Kernel Creation Script:"
echo

echo "---------------"
echo "Kernel Version:"
echo "---------------"

echo -e "${blue}"; echo -e "${blink_blue}"; echo "$YARPIIN_VER"; echo -e "${restore}";

echo -e "${green}"
echo "-----------------"
echo "Making Kernel:"
echo "-----------------"
echo -e "${restore}"

while read -p "Do you want to clean stuffs (y/n)[e(ndzip)/a(llmake)]? " cchoice
do
case "$cchoice" in
	y|Y )
		clean_all
		echo
		echo "All Cleaned now."
		break
		;;
	e|E )
		make_zip
		echo
		echo "Kernel Zipped!"
		echo
		exit
		break
		;;
	a|A )
		clean_all
		make_star2_kernel
		make_star2b_kernel
		clean_all
		make_crown_kernel
		make_crownb_kernel
		clean_all
		make_star_kernel
		make_starb_kernel
		make_zip
		clean_all
		echo
		echo "Every Thing Builded, Cleaned and Zipped!"
		echo
		exit
		break
		;;
	n|N )
		break
		;;
	* )
		echo
		echo "Invalid try again!"
		echo
		;;
esac
done

echo

while read -p "Do you want to build G965 kernel (y/n)? " dchoice
do
case "$dchoice" in
	y|Y)
		make_star2_kernel
		break
		;;
	n|N )
		break
		;;
	* )
		echo
		echo "Invalid try again!"
		echo
		;;
esac
done

echo

while read -p "Do you want to build G965 Battery kernel (y/n)? " dchoice
do
case "$dchoice" in
	y|Y)
		make_star2b_kernel
		break
		;;
	n|N )
		break
		;;
	* )
		echo
		echo "Invalid try again!"
		echo
		;;
esac
done

echo

while read -p "Do you want to clean stuffs (y/n)? " cchoice
do
case "$cchoice" in
	y|Y )
		clean_all
		echo
		echo "All Cleaned now."
		break
		;;
	n|N )
		break
		;;
	* )
		echo
		echo "Invalid try again!"
		echo
		;;
esac
done

echo

while read -p "Do you want to build G960 kernel (y/n)? " dchoice
do
case "$dchoice" in
	y|Y)
		make_star_kernel
		break
		;;
	n|N )
		break
		;;
	* )
		echo
		echo "Invalid try again!"
		echo
		;;
esac
done

echo

while read -p "Do you want to build G960 Battery kernel (y/n)? " dchoice
do
case "$dchoice" in
	y|Y)
		make_starb_kernel
		break
		;;
	n|N )
		break
		;;
	* )
		echo
		echo "Invalid try again!"
		echo
		;;
esac
done

echo

while read -p "Do you want to clean stuffs (y/n)? " cchoice
do
case "$cchoice" in
	y|Y )
		clean_all
		echo
		echo "All Cleaned now."
		break
		;;
	n|N )
		break
		;;
	* )
		echo
		echo "Invalid try again!"
		echo
		;;
esac
done

echo

while read -p "Do you want to build N960 kernel (y/n)? " dchoice
do
case "$dchoice" in
	y|Y)
		make_crown_kernel
		break
		;;
	n|N )
		break
		;;
	* )
		echo
		echo "Invalid try again!"
		echo
		;;
esac
done

echo

while read -p "Do you want to build N960 Battery kernel (y/n)? " dchoice
do
case "$dchoice" in
	y|Y)
		make_crownb_kernel
		break
		;;
	n|N )
		break
		;;
	* )
		echo
		echo "Invalid try again!"
		echo
		;;
esac
done

echo

while read -p "Do you want to zip kernel (y/n)? " dchoice
do
case "$dchoice" in
	y|Y)
		make_zip
		break
		;;
	n|N )
		break
		;;
	* )
		echo
		echo "Invalid try again!"
		echo
		;;
esac
done

echo -e "${green}"
echo "-------------------"
echo "Build Completed in:"
echo "-------------------"
echo -e "${restore}"

DATE_END=$(date +"%s")
DIFF=$(($DATE_END - $DATE_START))
echo "Time: $(($DIFF / 60)) minute(s) and $(($DIFF % 60)) seconds."
echo

