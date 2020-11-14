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

CPU=$(cat /tmp/aroma/cpu.prop | cut -d '=' -f2)
ZRAM=$(cat /tmp/aroma/zram.prop | cut -d '=' -f2)

# Patch fstab
mount -o remount,rw /vendor;
mount -o remount,rw /system;

if [ $CPU = 1 ]; then
  echo "    write /sys/power/cpuhotplug/governor/dual_freq 2704000" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    write /sys/power/cpuhotplug/governor/triple_freq 2496000" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    write /sys/power/cpuhotplug/governor/quad_freq 2314000" >> /tmp/anykernel/ramdisk/init.services.rc
elif [ $CPU = 2 ]; then
  echo "    write /sys/power/cpuhotplug/governor/dual_freq 2496000" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    write /sys/power/cpuhotplug/governor/triple_freq 2314000" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    write /sys/power/cpuhotplug/governor/quad_freq 2106000" >> /tmp/anykernel/ramdisk/init.services.rc
elif [ $CPU = 3 ]; then
  echo "    write /sys/power/cpuhotplug/governor/dual_freq 2496000" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    write /sys/power/cpuhotplug/governor/triple_freq 2106000" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    write /sys/power/cpuhotplug/governor/quad_freq 1924000" >> /tmp/anykernel/ramdisk/init.services.rc
elif [ $CPU = 5 ]; then
  echo "    write /sys/power/cpuhotplug/governor/dual_freq 2002000" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    write /sys/power/cpuhotplug/governor/triple_freq 1924000" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    write /sys/power/cpuhotplug/governor/quad_freq 1690000" >> /tmp/anykernel/ramdisk/init.services.rc
elif [ $CPU = 6 ]; then
  echo "    write /sys/power/cpuhotplug/governor/dual_freq 1924000" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    write /sys/power/cpuhotplug/governor/triple_freq 1794000" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    write /sys/power/cpuhotplug/governor/quad_freq 1586000" >> /tmp/anykernel/ramdisk/init.services.rc
elif [ $CPU = 7 ]; then
  echo "    write /sys/power/cpuhotplug/governor/dual_freq 1794000" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    write /sys/power/cpuhotplug/governor/triple_freq 1586000" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    write /sys/power/cpuhotplug/governor/quad_freq 1469000" >> /tmp/anykernel/ramdisk/init.services.rc
else
  echo "    write /sys/power/cpuhotplug/governor/dual_freq 2314000" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    write /sys/power/cpuhotplug/governor/triple_freq 1794000" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    write /sys/power/cpuhotplug/governor/quad_freq 1794000" >> /tmp/anykernel/ramdisk/init.services.rc
fi;
if [ $ZRAM != 1 ]; then
  echo "    write /sys/fs/selinux/enforce 0" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    write /sys/block/zram0/comp_algorithm zstd" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    write /proc/sys/vm/page-cluster 0" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    start swapon_zram" >> /tmp/anykernel/ramdisk/init.services.rc
fi;
echo "    write /sys/fs/selinux/enforce 1" >> /tmp/anykernel/ramdisk/init.services.rc
echo " " >> /tmp/anykernel/ramdisk/init.services.rc
echo "on property:sys.boot_completed=1" >> /tmp/anykernel/ramdisk/init.services.rc
echo "    stop proca" >> /tmp/anykernel/ramdisk/init.services.rc
echo "    stop secure_storage" >> /tmp/anykernel/ramdisk/init.services.rc
echo "    start sysinit" >> /tmp/anykernel/ramdisk/init.services.rc
echo " " >> /tmp/anykernel/ramdisk/init.services.rc
echo "service sysinit /sbin/sysinit.sh" >> /tmp/anykernel/ramdisk/init.services.rc
echo "    class late_start" >> /tmp/anykernel/ramdisk/init.services.rc
echo "    user root" >> /tmp/anykernel/ramdisk/init.services.rc
echo "    seclabel u:r:init:s0" >> /tmp/anykernel/ramdisk/init.services.rc
echo "    oneshot" >> /tmp/anykernel/ramdisk/init.services.rc
echo "    disabled" >> /tmp/anykernel/ramdisk/init.services.rc
echo " " >> /tmp/anykernel/ramdisk/init.services.rc
if [ $ZRAM = 2 ]; then
  echo "service swapon_zram /sbin/sswap -s -z -f 4096" >> /tmp/anykernel/ramdisk/init.services.rc
elif [ $ZRAM = 3 ]; then
  echo "service swapon_zram /sbin/sswap -s -z -f 3072" >> /tmp/anykernel/ramdisk/init.services.rc
elif [ $ZRAM = 4 ]; then
  echo "service swapon_zram /sbin/sswap -s -z -f 2560" >> /tmp/anykernel/ramdisk/init.services.rc
elif [ $ZRAM = 5 ]; then
  echo "service swapon_zram /sbin/sswap -s -z -f 2048" >> /tmp/anykernel/ramdisk/init.services.rc
elif [ $ZRAM = 6 ]; then
  echo "service swapon_zram /sbin/sswap -s -z -f 1536" >> /tmp/anykernel/ramdisk/init.services.rc
fi;
if [ $ZRAM != 1 ]; then
  echo "    class core" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    user root" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    group root" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    seclabel u:r:sswap:s0" >> /tmp/anykernel/ramdisk/init.services.rc
  echo "    oneshot" >> /tmp/anykernel/ramdisk/init.services.rc
  echo " " >> /tmp/anykernel/ramdisk/init.services.rc
fi;

if [ -d /system/product/vendor_overlay ]; then
    cp -af /tmp/anykernel/ramdisk/init.services.rc /system/product/vendor_overlay/29/etc/init/;
    cp -af /tmp/anykernel/ramdisk/fstab.samsungexynos9810 /system/product/vendor_overlay/29/etc/;
else
    cp -af /tmp/anykernel/ramdisk/init.services.rc /vendor/etc/init/;
    cp -af /tmp/anykernel/ramdisk/fstab.samsungexynos9810 /vendor/etc/;
fi;

# Move device dtb
mv -f $home/dtb.img $split_img/extra;

write_boot;
## end install

