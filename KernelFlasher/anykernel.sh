# AnyKernel3 Ramdisk Mod Script
# osm0sis @ xda-developers

## AnyKernel setup
# begin properties
properties() { '
kernel.string=
do.devicecheck=1
do.modules=0
do.systemless=1
do.cleanup=1
do.cleanuponabort=0
device.name1=starlte
device.name4=starltexx
device.name2=star2lte
device.name5=star2ltexx
device.name3=crownlte
device.name6=crownltexx
supported.versions=
supported.patchlevels=
'; } # end properties

# shell variables
block=/dev/block/platform/11120000.ufs/by-name/BOOT;
is_slot_device=auto;
ramdisk_compression=auto;


## AnyKernel methods (DO NOT CHANGE)
# import patching functions/variables - see for reference
. tools/ak3-core.sh;

## AnyKernel file attributes
# set permissions/ownership for included ramdisk files
set_perm_recursive 0 0 755 644 $ramdisk/*;
set_perm_recursive 0 0 750 750 $ramdisk/init* $ramdisk/sbin;

## AnyKernel install
dump_boot;

# Patch fstab
mount -o remount,rw /vendor;
mount -o remount,rw /system;

if [ -d /system/product/vendor_overlay ]; then
    if [ ! -e /system/product/vendor_overlay/29/etc/fstab.samsungexynos9810~ ]; then
	    backup_file /system/product/vendor_overlay/29/etc/fstab.samsungexynos9810;
    fi;
    cp -af /tmp/anykernel/ramdisk/init.services.rc /system/product/vendor_overlay/29/etc/init/;
    cp -af /tmp/anykernel/ramdisk/fstab.samsungexynos9810 /system/product/vendor_overlay/29/etc/;
else
    if [ ! -e /vendor/etc/fstab.samsungexynos9810~ ]; then
	backup_file /vendor/etc/fstab.samsungexynos9810;
    fi;
    cp -af /tmp/anykernel/ramdisk/init.services.rc /vendor/etc/init/;
    cp -af /tmp/anykernel/ramdisk/fstab.samsungexynos9810 /vendor/etc/;
fi;

# Move device dtb
mv -f $home/dtb.img $split_img/extra;

write_boot;
## end install

