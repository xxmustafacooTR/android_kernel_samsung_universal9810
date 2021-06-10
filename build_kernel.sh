#!/bin/bash

export ARCH=arm64
export CROSS_COMPILE=/home/kali/Android/Toolchains/aarch64-cruel-elf/bin/aarch64-cruel-elf-
export CROSS_COMPILE_ARM32=/home/kali/Android/Toolchains/arm-xxmustafacooTR-eabi/bin/arm-xxmustafacooTR-eabi-
export CC=/home/kali/Android/Toolchains/proton-clang/bin/clang
export ANDROID_MAJOR_VERSION=r

# Build dirs
KERNEL_DIR="/home/kali/Android/Kernel/android_kernel_samsung_universal9810"
KERNELFLASHER_DIR="/home/kali/Android/Kernel/Zip/"
if [ ! -d $KERNELFLASHER_DIR ]
then
     mkdir $KERNELFLASHER_DIR
fi;
ZIMAGE_DIR="$KERNEL_DIR/arch/arm64/boot"

# Resources
KERNEL="Image"
DTBIMAGE="dtb.img"

# Defconfigs
STARDEFCONFIG="exynos9810-starlte_defconfig"
STAR2DEFCONFIG="exynos9810-star2lte_defconfig"
CROWNDEFCONFIG="exynos9810-crownlte_defconfig"

echo "Write 'ulimit -n unlimited' to current terminal"
function clean_all {
		if [ -f "$MODULES_DIR/*.ko" ]; then
			rm `echo $MODULES_DIR"/*.ko"`
		fi
		cd $KERNEL_DIR
		echo
		rm -rf drivers/gator_5.27/gator_src_md5.h scripts/dtbtool_exynos/dtbtool arch/arm64/boot/dtb.img arch/arm64/boot/dts/exynos/*dtb*
		make -j$(nproc) clean
		make -j$(nproc) mrproper
        	rm -rf out/
}

function make_star2_kernel {
	echo
        make ARCH=arm64 $STAR2DEFCONFIG
	make -j$(nproc --all)

	cp -vr $ZIMAGE_DIR/$KERNEL $KERNELFLASHER_DIR/Kernel/G965zImage
        cp -vr $ZIMAGE_DIR/$DTBIMAGE $KERNELFLASHER_DIR/Kernel/G965dtb.img
}

function make_star_kernel {
	echo
	make ARCH=arm64 $STARDEFCONFIG
	make -j$(nproc --all)

	cp -vr $ZIMAGE_DIR/$KERNEL $KERNELFLASHER_DIR/Kernel/G960zImage
        cp -vr $ZIMAGE_DIR/$DTBIMAGE $KERNELFLASHER_DIR/Kernel/G960dtb.img
}

function make_crown_kernel {
	echo
        make ARCH=arm64 $CROWNDEFCONFIG
	make -j$(nproc --all)

	cp -vr $ZIMAGE_DIR/$KERNEL $KERNELFLASHER_DIR/Kernel/N960zImage
        cp -vr $ZIMAGE_DIR/$DTBIMAGE $KERNELFLASHER_DIR/Kernel/N960dtb.img
}

while read -p "Do you want to clean stuffs (y/n)[a(llmake)]? " cchoice
do
case "$cchoice" in
	y|Y )
		clean_all
		echo
		echo "All Cleaned now."
		break
		;;
	a|A )
		make_star2_kernel
		clean_all
		make_crown_kernel
		clean_all
		make_star_kernel
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

echo "-------------------"
echo "  Build Completed  "
echo "-------------------"