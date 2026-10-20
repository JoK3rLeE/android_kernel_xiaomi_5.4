/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 */

#define CONFIG_SPECTRA_CAMERA_MSMNILE 1
#define CONFIG_SPECTRA_CAMERA 1
#define CONFIG_SPECTRA_ISP 1
#define CONFIG_SPECTRA_ICP 1
#define CONFIG_SPECTRA_JPEG 1
#define CONFIG_SPECTRA_FD 1
#define CONFIG_SPECTRA_LRME 1
#define CONFIG_SPECTRA_SENSOR 1

/*
 * Cepheus uses the AK7374 voice-coil actuator on its secondary camera
 * module. The 5.4 actuator core already contains the anti-actuator-noise
 * sequence; it is guarded by this legacy config macro rather than Kconfig.
 * Keep it Cepheus-only. The routine additionally checks the CCI SID before
 * touching the actuator registers.
 */
#ifdef CONFIG_MACH_XIAOMI_CEPHEUS
#define CONFIG_VCM_AK7374 1
#endif
