obj-y := core.o

obj-$(CONFIG_BPF_SYSCALL) += syscall.o verifier.o inode.o helpers.o
obj-$(CONFIG_BPF_SYSCALL) += hashtab.o arraymap.o percpu_freelist.o
ifeq ($(CONFIG_PERF_EVENTS),y)
obj-$(CONFIG_BPF_SYSCALL) += stackmap.o
endif
obj-$(CONFIG_CGROUP_BPF) += cgroup.o

# various version of gcc have a LTO bug where the &&labels used in the
# BPF interpreter can cause linker errors when spread incorrectly over
# partitions. Disable LTO for BPF for now
CFLAGS_core.o = $(DISABLE_LTO)
