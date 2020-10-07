#!/bin/bash
## Exynos 9810 Endurance Kernel Build Script by @Eamo5 & Modified by THEBOSS
## Read before usage!

## Syntax
# For help, run $ ./make9810.sh help
# $ ./make9810.sh <device-varant> <branch> <gcc-version> <oc / uc> <test / release>
# eg. $ ./make9810.sh starlte oreo gcc-4.9 test

## To add:
# * Variable increment when using release parameter
# * Add flexibility to where parameters are entered
# * Work around messy outcomes highlighted in script
# * N9 UC support
# * Detect uncommited work and present confirmation prompt
# * Provide flexibility to other systems and allow for script to be placed in root of source

## Assign Variables
# Correctly assigning these variables makes the script relatively portable. 
# This is useful for me, as I need to compile on multiple systems. This will also aide anyone else trying to compile the kernel.
# File paths *must* have an '/' at the end, or else the script will generate various errors

# Versions
Q_VERSION=4\.9\.218

# Default Device
# Used as a backup when no valid device is defined
DEFAULT_DEVICE=star2lte
DEFAULT_DEVICE_DIRECTORY="$star2_KERNEL_DIRECTORY"

# Kernel Source Paths
star2_KERNEL_DIRECTORY=/home/kali/Android/Kernel/xxmustafacooTR/
TOOLCHAINS_DIRECTORY=/home/kali/Android/Toolchains/
KERNEL_DIR="/home/kali/Android/Kernel/xxmustafacooTR"
RESOURCE_DIR="$KERNEL_DIR/.."
KERNELFLASHER_DIR="/home/kali/Android/Kernel/Build/KernelFlasher"
TOOLCHAIN_DIR="$TOOLCHAINS_DIRECTORY"

# Resources
THREAD="-j$(grep -c ^processor /proc/cpuinfo)"
KERNEL="Image"
DTBIMAGE="dtb.img"

# Android Image Kitchen paths
AIK_960=/home/kali/Android/Kernel/Build/KernelFlasher/

# Zip directories
ZIP_960=/home/kali/Android/Kernel/Zip/

# Image dirs
ZIP_MOVE="/home/kali/Android/Kernel/Zip"
ZIMAGE_DIR="$KERNEL_DIR/arch/arm64/boot"

# Kernel Details
BASE_YARPIIN_VER="xxmustafacooTR"
VER="-030-OC"
PERM="-PERM"
YARPIIN_VER="$BASE_YARPIIN_VER$VER"
YARPIIN_PERM_VER="$BASE_YARPIIN_VER$VER$PERM"
STAR_VER=""
STAR2_VER=""
CROWN_VER=""

# Password for AIK sudo
PASSWORD=kali

## Functions
# This function should help simplify and compress the code a little bit.
# When passing parameters, the following syntax should be following.
# $ sed_func "OLD_STRING" "NEW_STRING" "PATH"
function clean_all {
	rm -f arch/arm64/boot/dtb.img arch/arm64/boot/dts/exynos/exynos9810-star2lte_eur_open_26.dtb arch/arm64/boot/dts/exynos/exynos9810-star2lte_eur_open_26.dtb.reverse.dts arch/arm64/boot/dts/exynos/exynos9810-starlte_eur_open_26.dtb arch/arm64/boot/dts/exynos/exynos9810-starlte_eur_open_26.dtb.reverse.dts arch/arm64/boot/dts/exynos/exynos9810-crownlte_eur_open_26.dtb arch/arm64/boot/dts/exynos/exynos9810-crownlte_eur_open_26.dtb.reverse.dts
        rm -rf out/
	make -j$(nproc) clean
	make -j$(nproc) mrproper
}
sed_func () {
	sed -i "s/"$1";/"$2";/g" "$3"
}

pie_oc_star2 () {
	sed -i 's/upscale_ratio_table = < 55 962000 65 1261000 90 >;/upscale_ratio_table = < 55 962000 65 1261000 80 >;/g' "$star2_KERNEL_DIRECTORY"arch/arm64/boot/dts/exynos/exynos9810.dtsi
	sed -i 's/unsigned long arg_cpu_max_c2 = 2704000;/unsigned long arg_cpu_max_c2 = 2964000;/g' "$star2_KERNEL_DIRECTORY"drivers/cpufreq/exynos-acme.c
	sed -i 's/static unsigned long arg_cpu_max_c1 = 1794000;/static unsigned long arg_cpu_max_c1 = 2002000;/g' "$star2_KERNEL_DIRECTORY"drivers/cpufreq/exynos-acme.c
#	sed -i 's/quad_freq = <1794000>;/quad_freq = <2106000>;/g' "$star2_KERNEL_DIRECTORY"arch/arm64/boot/dts/exynos/exynos9810.dtsi
#	sed -i 's/triple_freq = <1794000>;/triple_freq = <2106000>;/g' "$star2_KERNEL_DIRECTORY"arch/arm64/boot/dts/exynos/exynos9810.dtsi
#	sed -i 's/dual_freq = <2314000>;/dual_freq = <2496000>;/g' "$star2_KERNEL_DIRECTORY"arch/arm64/boot/dts/exynos/exynos9810.dtsi
	sed -i 's/2158000/2106000/g' "$star2_KERNEL_DIRECTORY"arch/arm64/boot/dts/exynos/exynos9810.dtsi
}

oreo_oc_star2 () {
	sed -i 's/upscale_ratio_table = < 55 962000 65 1261000 90 >;/upscale_ratio_table = < 75 962000 80 1066000 85 1170000 90 1261000 95 >;/g' "$star2_KERNEL_DIRECTORY"arch/arm64/boot/dts/exynos/exynos9810.dtsi
#	sed -i 's/unsigned long arg_cpu_max_c2 = 2704000;/unsigned long arg_cpu_max_c2 = 2964000;/g' "$star2_KERNEL_DIRECTORY"drivers/cpufreq/exynos-acme.c
#	sed -i 's/static unsigned long arg_cpu_max_c1 = 1794000;/static unsigned long arg_cpu_max_c1 = 2002000;/g' "$star2_KERNEL_DIRECTORY"drivers/cpufreq/exynos-acme.c
#	sed -i 's/quad_freq = <1794000>;/quad_freq = <2106000>;/g' "$star2_KERNEL_DIRECTORY"arch/arm64/boot/dts/exynos/exynos9810.dtsi
#	sed -i 's/triple_freq = <1794000>;/triple_freq = <2106000>;/g' "$star2_KERNEL_DIRECTORY"arch/arm64/boot/dts/exynos/exynos9810.dtsi
#	sed -i 's/dual_freq = <2314000>;/dual_freq = <2496000>;/g' "$star2_KERNEL_DIRECTORY"arch/arm64/boot/dts/exynos/exynos9810.dtsi
#	sed -i 's/2158000/2106000/g' "$star2_KERNEL_DIRECTORY"arch/arm64/boot/dts/exynos/exynos9810.dtsi
}

bsdiff_func () {
	bsdiff boot-G965F.img boot-G960F.img boot-G960F.img.bsdiff
	bsdiff boot-G960F-oc.img boot-G960F.img boot-G960F-oc.img.bsdiff
	bsdiff boot-G960F-uc.img boot-G960F.img boot-G960F-uc.img.bsdiff
	bsdiff boot-G965F-oc.img boot-G965F.img boot-G965F-oc.img.bsdiff
	bsdiff boot-G965F-uc.img boot-G965F.img boot-G965F-uc.img.bsdiff
}

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

## Help

if [ "$1" == "help" ] || [ "$1" == "" ]; then
	echo ""
	echo "Howdy!"
	echo ""
	echo "How to use this script:"
	echo ""
	echo "1. Assign Variables"
	echo ""
	echo "Correctly assigning variables at the start of the script makes the script relatively portable."
	echo "This means the entire build script can be pretty easily adapted to a different system. eg. Yours!"
	echo "To assign variables you can directly modify this script (make9810.sh)"
	echo "The default device assigned is starlte."
	echo ""
	echo "2. Syntax"
	echo ""
	echo "$ ./make9810.sh <device-varant> <branch> <gcc-version> <oc / uc> <test / release> <update>"
	echo "<oc / uc> and <test / release> arguments are optional."
	echo ""
	echo "3. Compatibility"
	echo ""
	echo "Valid Devices - 'starlte' 'star2lte' 'star2lte' - Not setting a valid device will get you as far as kernels being compiled if the scripts assumptions of the default device are correct. AIK and zipping *must* be done manually"
	echo "Valid Branches - 'pie' 'oreo' 'apgk' 'els' '9.0-aosp' 'gsi' '8.1-aosp' - Script will attempt to work with branch by default and will assume your default device from the assigned variables below."
	echo "If you're lazy you can rename a branch and subsitute it in."
	echo "Valid GCC Versions - 'gcc-8' gcc-4.9' - If no valid parameters are passed, the script defaults to the current branch with GCC 4.9."
	echo "Overclocking / Underclocking - 'oc' 'uc' or this can be ignored entirely and test / release can be put in OC / UC's place."
	echo "Update - If passing the update argument, relevant scripts will be updated with the correct kernel and upstream version. This can be run individually or as an additional argument."
	echo ""
	echo "4. Test vs Release"
	echo ""
	echo "Test - If the argument 'release' is passed in place of OC / UC, the script will create relevant patches with only the base boot.img's present and zip it."
	echo "This will only zip the core boot-G965F.img & boot-G960F.img.bsdiff."
	echo ""
	echo "Release - If the argument 'release' is passed after or in place of OC / UC, the script will create relevant patches with all standard, OC & UC boot.img's present and zip it."
	echo "Hence when building the last kernel (in the last call of the script) you'll want to pass 'release'."
	echo "Refer to make9810release.sh for a reference example for how I create a release zip!"
	echo ""
	exit
fi

## Kernel Directory

if [ "$1" == "star2lte" ]; then
	cd "$star2_KERNEL_DIRECTORY" || exit
else
	echo "Did not define a valid specific device!"
	echo "Correct syntax is as follows: ./make9810.sh <device-variant> <branch> <gcc-version> <oc / uc> <test / release>"
	echo "eg. $ ./make9810.sh starlte oreo gcc-4.9 test"
	echo "Defaulting to star kernel directory..."
	cd "$DEFAULT_DEVICE_DIRECTORY" || exit
fi

## Build environment

export ARCH=arm64
export SUBARCH=arm64

# Check for specified GCC parameters. If none passed, use GCC 4.9 by default + use ccache to improve compile speeds

if [ "$3" == "gcc-9.1" ]; then
	export CROSS_COMPILE="ccache "$TOOLCHAINS_DIRECTORY"linaro-64/bin/aarch64-linux-gnu-"
elif [ "$3" == "gcc-11" ]; then
	export CROSS_COMPILE="ccache "$TOOLCHAINS_DIRECTORY"GCC-11/bin/aarch64-linux-elf-"
	export PLATFORM_VERSION=10.0.0
	export ANDROID_MAJOR_VERSION=q
	export CROSS_COMPILE_ARM32="ccache "$TOOLCHAINS_DIRECTORY"gcc-9.2-arm-none-eabi/bin/arm-none-eabi-"
	export LDLLD="ccache "$TOOLCHAINS_DIRECTORY"DragonTC-CLANG-9.0.5/bin/ld.lld"
	export CC="ccache "$TOOLCHAINS_DIRECTORY"DragonTC-CLANG-9.0.5/bin/clang"
	export CLANG_TRIPLE="ccache "$TOOLCHAINS_DIRECTORY"GCC-11/bin/aarch64-linux-elf-"
elif [ "$3" == "gcc-4.9" ]; then
	export CROSS_COMPILE="ccache "$TOOLCHAINS_DIRECTORY"aarch64-linux-android-4.9/bin/aarch64-linux-android-"
	export PLATFORM_VERSION=10.0.0
	export ANDROID_MAJOR_VERSION=q
else
	export CROSS_COMPILE="ccache "$TOOLCHAINS_DIRECTORY"aarch64-linux-android-4.9/bin/aarch64-linux-android-"
	export PLATFORM_VERSION=10.0.0
	export ANDROID_MAJOR_VERSION=q
	export CROSS_COMPILE_ARM32="ccache "$TOOLCHAINS_DIRECTORY"gcc-9.2-arm-none-eabi/bin/arm-none-eabi-"
	export LDLLD="ccache "$TOOLCHAINS_DIRECTORY"DragonTC-CLANG-9.0.5/bin/ld.lld"
	export CC="ccache "$TOOLCHAINS_DIRECTORY"DragonTC-CLANG-9.0.5/bin/clang"
	export CLANG_TRIPLE="ccache "$TOOLCHAINS_DIRECTORY"aarch64-linux-android-4.9/bin/aarch64-linux-android-"
fi

## Build Specific Preperation
# Includes specific modifications for OC and UC on the kernel, as well as modifies the kernels localversion on the fly.

if [ "$2" == "oreo" ]; then
	if [ "$1" == "star2lte" ] && [ "$4" == "oc" ]; then
		sed -i "s/-THEBOSS-Zeus-OC-/-THEBOSS-Zeus-OC-N9-"$OREO_VERSION"/g" "$star2_KERNEL_DIRECTORY"arch/arm64/configs/exynos9810-star2lte_defconfig
		oreo_oc_star2
	elif [ "$1" == "star2lte" ] && [ "$4" == "oc" ]; then
		sed -i "s/-THEBOSS-Zeus-OC-/-THEBOSS-Zeus-OC-N9-"$OREO_VERSION"/g" "$star2_KERNEL_DIRECTORY"arch/arm64/configs/exynos9810-star2lte_defconfig
	elif [ "$1" == "star2lte" ] && [ "$4" != "uc" ]; then
		sed -i "s/-THEBOSS-Zeus-OC-/-THEBOSS-Zeus-OC-N9-"$OREO_VERSION"/g" "$star2_KERNEL_DIRECTORY"arch/arm64/configs/exynos9810-star2lte_defconfig
	else
		echo "Invalid device or OC configuration detected... Please check your inputs."
	fi
fi

## Make



if [ "$2" == "oreo" ] || [ "$2" == "apgk" ] || [ "$2" == "gsi" ] || [ "$2" == "8.1-aosp" ]; then
	if [ "$1" == "star2lte" ]; then
		make -j$(nproc) exynos9810-star2lte_defconfig
	else
		echo "Incorrect device variant configuration..."
		echo "Using default device"
		make -j$(nproc) exynos9810-"$DEFAULT_DEVICE"_defconfig
	fi
else
	echo "Did not define a known branch. Defaulting to using standard default device defconfig."
	make -j$(nproc) exynos9810-"$DEFAULT_DEVICE"_defconfig
fi
make -j$(nproc --all)

## AIK Preparation

cp -vr $ZIMAGE_DIR/$KERNEL $KERNELFLASHER_DIR/Kernel/zImage
cp -vr $ZIMAGE_DIR/$DTBIMAGE $KERNELFLASHER_DIR/Kernel/dtb.img

function make_zip {
		cd $AIK_960
		zip -r9 `echo $YARPIIN_VER`.zip *
		mv  `echo $YARPIIN_VER`.zip $ZIP_MOVE
		cd $ZIP_960
}

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

## Cleanup
