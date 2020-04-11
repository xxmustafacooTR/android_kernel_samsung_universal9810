# Exynos FMP/SMU makefile
#ccflags-y := -DCONFIG_EXYNOS_FMP_FIPS_FUNC_TEST
obj-$(CONFIG_EXYNOS_SMU) += smu_dev.o
CC_NO_CLANG := $(shell $(CC) -dM -E -x c /dev/null | grep -Fq "__clang__"; echo $$?)
ifeq ($(CC_NO_CLANG), 0)
ccflags-y += -DCC_USE_CLANG
CFLAGS_fmp_fips_selftest.o = -fno-merge-all-constants
endif
ifeq ($(CC_NO_CLANG), 1)
CFLAGS_fmp_fips_selftest.o = -fno-merge-constants
endif
obj-$(CONFIG_EXYNOS_FMP_FIPS) += first_file.o
obj-$(CONFIG_EXYNOS_FMP) += fmp_dev.o fmp_lib.o
obj-$(CONFIG_EXYNOS_FMP_FIPS) += fmp_fips_main.o fmp_fips_fops.o fmp_fips_selftest.o \
				fmp_fips_integrity.o hmac-sha256.o \
				fmp_fips_cipher.o
ifneq ($(filter y, $(CONFIG_EXYNOS_FMP) $(CONFIG_EXYNOS_FMP_FIPS)),)
obj-y += sha256.o
endif
#obj-y += fmp_fips_func_test.o
obj-$(CONFIG_EXYNOS_FMP_FIPS) += last_file.o
obj-$(CONFIG_EXYNOS_FMP_FIPS) += fips_out.o
obj-$(CONFIG_EXYNOS_FMP) += fmp_derive_iv.o

# KASAN White list
KASAN_SANITIZE_sha256.o := n
