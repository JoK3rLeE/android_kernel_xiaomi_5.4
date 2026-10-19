# SPDX-License-Identifier: GPL-2.0

# Xiaomi SM8150 board bases and Cepheus device overlay.
ifeq ($(CONFIG_MACH_XIAOMI_SM8150),y)

dtb-$(CONFIG_ARCH_SM8150) += sm8150-xiaomi.dtb sm8150-v2-xiaomi.dtb

ifeq ($(CONFIG_MACH_XIAOMI_CEPHEUS),y)
ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
dtbo-$(CONFIG_ARCH_SM8150) += cepheus-sm8150-overlay.dtbo
cepheus-sm8150-overlay.dtbo-base := sm8150-xiaomi.dtb sm8150-v2-xiaomi.dtb
endif
endif

endif
