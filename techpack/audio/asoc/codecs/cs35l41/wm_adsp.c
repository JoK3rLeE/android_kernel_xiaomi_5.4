/*
 * wm_adsp.c  --  Wolfson ADSP support
 *
 * Copyright 2012 Wolfson Microelectronics plc
 * Copyright (C) 2019 XiaoMi, Inc.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define DEBUG
//#define WM_ADSP_DEBUG
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/list.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm_adsp.h"

#define adsp_crit(_dsp, fmt, ...)                                              \
	dev_crit(_dsp->dev, "DSP%d: " fmt, _dsp->num, ##__VA_ARGS__)
#define adsp_err(_dsp, fmt, ...)                                               \
	dev_err(_dsp->dev, "DSP%d: " fmt, _dsp->num, ##__VA_ARGS__)
#define adsp_warn(_dsp, fmt, ...)                                              \
	dev_warn(_dsp->dev, "DSP%d: " fmt, _dsp->num, ##__VA_ARGS__)
#define adsp_info(_dsp, fmt, ...)                                              \
	dev_info(_dsp->dev, "DSP%d: " fmt, _dsp->num, ##__VA_ARGS__)
#define adsp_dbg(_dsp, fmt, ...)                                               \
	dev_info(_dsp->dev, "DSP%d: " fmt, _dsp->num, ##__VA_ARGS__)

#define MAXBULK 4 /* Max byte I2C bulk limit */

#define ADSP1_CONTROL_1 0x00
#define ADSP1_CONTROL_2 0x02
#define ADSP1_CONTROL_3 0x03
#define ADSP1_CONTROL_4 0x04
#define ADSP1_CONTROL_5 0x06
#define ADSP1_CONTROL_6 0x07
#define ADSP1_CONTROL_7 0x08
#define ADSP1_CONTROL_8 0x09
#define ADSP1_CONTROL_9 0x0A
#define ADSP1_CONTROL_10 0x0B
#define ADSP1_CONTROL_11 0x0C
#define ADSP1_CONTROL_12 0x0D
#define ADSP1_CONTROL_13 0x0F
#define ADSP1_CONTROL_14 0x10
#define ADSP1_CONTROL_15 0x11
#define ADSP1_CONTROL_16 0x12
#define ADSP1_CONTROL_17 0x13
#define ADSP1_CONTROL_18 0x14
#define ADSP1_CONTROL_19 0x16
#define ADSP1_CONTROL_20 0x17
#define ADSP1_CONTROL_21 0x18
#define ADSP1_CONTROL_22 0x1A
#define ADSP1_CONTROL_23 0x1B
#define ADSP1_CONTROL_24 0x1C
#define ADSP1_CONTROL_25 0x1E
#define ADSP1_CONTROL_26 0x20
#define ADSP1_CONTROL_27 0x21
#define ADSP1_CONTROL_28 0x22
#define ADSP1_CONTROL_29 0x23
#define ADSP1_CONTROL_30 0x24
#define ADSP1_CONTROL_31 0x26

/*
 * ADSP1 Control 19
 */
#define ADSP1_WDMA_BUFFER_LENGTH_MASK                                          \
	0x00FF /* DSP1_WDMA_BUFFER_LENGTH - [7:0] */
#define ADSP1_WDMA_BUFFER_LENGTH_SHIFT 0 /* DSP1_WDMA_BUFFER_LENGTH - [7:0] */
#define ADSP1_WDMA_BUFFER_LENGTH_WIDTH 8 /* DSP1_WDMA_BUFFER_LENGTH - [7:0] */

/*
 * ADSP1 Control 30
 */
#define ADSP1_DBG_CLK_ENA 0x0008 /* DSP1_DBG_CLK_ENA */
#define ADSP1_DBG_CLK_ENA_MASK 0x0008 /* DSP1_DBG_CLK_ENA */
#define ADSP1_DBG_CLK_ENA_SHIFT 3 /* DSP1_DBG_CLK_ENA */
#define ADSP1_DBG_CLK_ENA_WIDTH 1 /* DSP1_DBG_CLK_ENA */
#define ADSP1_SYS_ENA 0x0004 /* DSP1_SYS_ENA */
#define ADSP1_SYS_ENA_MASK 0x0004 /* DSP1_SYS_ENA */
#define ADSP1_SYS_ENA_SHIFT 2 /* DSP1_SYS_ENA */
#define ADSP1_SYS_ENA_WIDTH 1 /* DSP1_SYS_ENA */
#define ADSP1_CORE_ENA 0x0002 /* DSP1_CORE_ENA */
#define ADSP1_CORE_ENA_MASK 0x0002 /* DSP1_CORE_ENA */
#define ADSP1_CORE_ENA_SHIFT 1 /* DSP1_CORE_ENA */
#define ADSP1_CORE_ENA_WIDTH 1 /* DSP1_CORE_ENA */
#define ADSP1_START 0x0001 /* DSP1_START */
#define ADSP1_START_MASK 0x0001 /* DSP1_START */
#define ADSP1_START_SHIFT 0 /* DSP1_START */
#define ADSP1_START_WIDTH 1 /* DSP1_START */

/*
 * ADSP1 Control 31
 */
#define ADSP1_CLK_SEL_MASK 0x0007 /* CLK_SEL_ENA */
#define ADSP1_CLK_SEL_SHIFT 0 /* CLK_SEL_ENA */
#define ADSP1_CLK_SEL_WIDTH 3 /* CLK_SEL_ENA */

#define ADSP2_CONTROL 0x0
#define ADSP2_CLOCKING 0x1
#define ADSP2V2_CLOCKING 0x2
#define ADSP2_STATUS1 0x4
#define ADSP2_WDMA_CONFIG_1 0x30
#define ADSP2_WDMA_CONFIG_2 0x31
#define ADSP2V2_WDMA_CONFIG_2 0x32
#define ADSP2_RDMA_CONFIG_1 0x34

#define ADSP2_SCRATCH0 0x40
#define ADSP2_SCRATCH1 0x41
#define ADSP2_SCRATCH2 0x42
#define ADSP2_SCRATCH3 0x43

#define ADSP2V2_SCRATCH0_1 0x40
#define ADSP2V2_SCRATCH2_3 0x42

/*
 * ADSP2 Control
 */

#define ADSP2_MEM_ENA 0x0010 /* DSP1_MEM_ENA */
#define ADSP2_MEM_ENA_MASK 0x0010 /* DSP1_MEM_ENA */
#define ADSP2_MEM_ENA_SHIFT 4 /* DSP1_MEM_ENA */
#define ADSP2_MEM_ENA_WIDTH 1 /* DSP1_MEM_ENA */
#define ADSP2_SYS_ENA 0x0004 /* DSP1_SYS_ENA */
#define ADSP2_SYS_ENA_MASK 0x0004 /* DSP1_SYS_ENA */
#define ADSP2_SYS_ENA_SHIFT 2 /* DSP1_SYS_ENA */
#define ADSP2_SYS_ENA_WIDTH 1 /* DSP1_SYS_ENA */
#define ADSP2_CORE_ENA 0x0002 /* DSP1_CORE_ENA */
#define ADSP2_CORE_ENA_MASK 0x0002 /* DSP1_CORE_ENA */
#define ADSP2_CORE_ENA_SHIFT 1 /* DSP1_CORE_ENA */
#define ADSP2_CORE_ENA_WIDTH 1 /* DSP1_CORE_ENA */
#define ADSP2_START 0x0001 /* DSP1_START */
#define ADSP2_START_MASK 0x0001 /* DSP1_START */
#define ADSP2_START_SHIFT 0 /* DSP1_START */
#define ADSP2_START_WIDTH 1 /* DSP1_START */

/*
 * ADSP2 clocking
 */
#define ADSP2_CLK_SEL_MASK 0x0007 /* CLK_SEL_ENA */
#define ADSP2_CLK_SEL_SHIFT 0 /* CLK_SEL_ENA */
#define ADSP2_CLK_SEL_WIDTH 3 /* CLK_SEL_ENA */

/*
 * ADSP2V2 clocking
 */
#define ADSP2V2_CLK_SEL_MASK 0x70000 /* CLK_SEL_ENA */
#define ADSP2V2_CLK_SEL_SHIFT 16 /* CLK_SEL_ENA */
#define ADSP2V2_CLK_SEL_WIDTH 3 /* CLK_SEL_ENA */

#define ADSP2V2_RATE_MASK 0x7800 /* DSP_RATE */
#define ADSP2V2_RATE_SHIFT 11 /* DSP_RATE */
#define ADSP2V2_RATE_WIDTH 4 /* DSP_RATE */

/*
 * ADSP2 Status 1
 */
#define ADSP2_RAM_RDY 0x0001
#define ADSP2_RAM_RDY_MASK 0x0001
#define ADSP2_RAM_RDY_SHIFT 0
#define ADSP2_RAM_RDY_WIDTH 1

/*
 * ADSP2 Lock support
 */
#define ADSP2_LOCK_CODE_0 0x5555
#define ADSP2_LOCK_CODE_1 0xAAAA

#define ADSP2_WATCHDOG 0x0A
#define ADSP2_BUS_ERR_ADDR 0x52
#define ADSP2_REGION_LOCK_STATUS 0x64
#define ADSP2_LOCK_REGION_1_LOCK_REGION_0 0x66
#define ADSP2_LOCK_REGION_3_LOCK_REGION_2 0x68
#define ADSP2_LOCK_REGION_5_LOCK_REGION_4 0x6A
#define ADSP2_LOCK_REGION_7_LOCK_REGION_6 0x6C
#define ADSP2_LOCK_REGION_9_LOCK_REGION_8 0x6E
#define ADSP2_LOCK_REGION_CTRL 0x7A
#define ADSP2_PMEM_ERR_ADDR_XMEM_ERR_ADDR 0x7C

#define ADSP2_REGION_LOCK_ERR_MASK 0x8000
#define ADSP2_SLAVE_ERR_MASK 0x4000
#define ADSP2_WDT_TIMEOUT_STS_MASK 0x2000
#define ADSP2_CTRL_ERR_PAUSE_ENA 0x0002
#define ADSP2_CTRL_ERR_EINT 0x0001

#define ADSP2_BUS_ERR_ADDR_MASK 0x00FFFFFF
#define ADSP2_XMEM_ERR_ADDR_MASK 0x0000FFFF
#define ADSP2_PMEM_ERR_ADDR_MASK 0x7FFF0000
#define ADSP2_PMEM_ERR_ADDR_SHIFT 16
#define ADSP2_WDT_ENA_MASK 0xFFFFFFFD

#define ADSP2_LOCK_REGION_SHIFT 16

#define ADSP_MAX_STD_CTRL_SIZE 512

#define WM_ADSP_ACKED_CTL_TIMEOUT_MS 100
#define WM_ADSP_ACKED_CTL_N_QUICKPOLLS 10
#define WM_ADSP_ACKED_CTL_MIN_VALUE 0
#define WM_ADSP_ACKED_CTL_MAX_VALUE 0xFFFFFF

/*
 * Event control messages
 */
#define WM_ADSP_FW_EVENT_SHUTDOWN 0x000001

/*
 * HALO system info
 */
#define HALO_SYS_INFO_XM_SRAM_SIZE 0x00010
#define HALO_SYS_INFO_YM_SRAM_SIZE 0x00018
#define HALO_SYS_INFO_XM_BANK_SIZE 0x00038
#define HALO_SYS_INFO_YM_BANK_SIZE 0x0003c
#define HALO_AHBM_WINDOW_DEBUG_0 0x02040
#define HALO_AHBM_WINDOW_DEBUG_1 0x02044

/*
 * HALO core
 */
#define HALO_SAMPLE_RATE_RX1 0x00080
#define HALO_SAMPLE_RATE_TX1 0x00280
#define HALO_SCRATCH1 0x005c0
#define HALO_CCM_CORE_CONTROL 0x41000

/*
 * HALO Lock support
 */
#define HALO_MPU_UNLOCK_CODE_0 0x5555
#define HALO_MPU_UNLOCK_CODE_1 0xaaaa

/*
 * HALO MPU banks
 */
#define HALO_MPU_XMEM_ACCESS_0 0x43000
#define HALO_MPU_YMEM_ACCESS_0 0x43004
#define HALO_MPU_WINDOW_ACCESS_0 0x43008
#define HALO_MPU_XREG_ACCESS_0 0x4300C
#define HALO_MPU_YREG_ACCESS_0 0x43014
#define HALO_MPU_XMEM_ACCESS_1 0x43018
#define HALO_MPU_YMEM_ACCESS_1 0x4301C
#define HALO_MPU_WINDOW_ACCESS_1 0x43020
#define HALO_MPU_XREG_ACCESS_1 0x43024
#define HALO_MPU_YREG_ACCESS_1 0x4302C
#define HALO_MPU_XMEM_ACCESS_2 0x43030
#define HALO_MPU_YMEM_ACCESS_2 0x43034
#define HALO_MPU_WINDOW_ACCESS_2 0x43038
#define HALO_MPU_XREG_ACCESS_2 0x4303C
#define HALO_MPU_YREG_ACCESS_2 0x43044
#define HALO_MPU_XMEM_ACCESS_3 0x43048
#define HALO_MPU_YMEM_ACCESS_3 0x4304C
#define HALO_MPU_WINDOW_ACCESS_3 0x43050
#define HALO_MPU_XREG_ACCESS_3 0x43054
#define HALO_MPU_YREG_ACCESS_3 0x4305C
#define HALO_MPU_XM_VIO_ADDR 0x43100
#define HALO_MPU_XM_VIO_STATUS 0x43104
#define HALO_MPU_YM_VIO_ADDR 0x43108
#define HALO_MPU_YM_VIO_STATUS 0x4310C
#define HALO_MPU_PM_VIO_ADDR 0x43110
#define HALO_MPU_PM_VIO_STATUS 0x43114
#define HALO_MPU_LOCK_CONFIG 0x43140

/*
 * HALO stream arb
 */
#define HALO_STREAM_ARB_MSTR0_CONFIG_0 0x45000
#define HALO_STREAM_ARB_MSTR0_CONFIG_1 0x45004
#define HALO_STREAM_ARB_MSTR0_CONFIG_2 0x45008
#define HALO_STREAM_ARB_MSTR1_CONFIG_0 0x45010
#define HALO_STREAM_ARB_MSTR1_CONFIG_1 0x45014
#define HALO_STREAM_ARB_MSTR1_CONFIG_2 0x45018
#define HALO_STREAM_ARB_MSTR2_CONFIG_0 0x45020
#define HALO_STREAM_ARB_MSTR2_CONFIG_1 0x45024
#define HALO_STREAM_ARB_MSTR2_CONFIG_2 0x45028
#define HALO_STREAM_ARB_MSTR3_CONFIG_0 0x45030
#define HALO_STREAM_ARB_MSTR3_CONFIG_1 0x45034
#define HALO_STREAM_ARB_MSTR3_CONFIG_2 0x45038
#define HALO_STREAM_ARB_MSTR4_CONFIG_0 0x45040
#define HALO_STREAM_ARB_MSTR4_CONFIG_1 0x45044
#define HALO_STREAM_ARB_MSTR4_CONFIG_2 0x45048
#define HALO_STREAM_ARB_MSTR5_CONFIG_0 0x45050
#define HALO_STREAM_ARB_MSTR5_CONFIG_1 0x45054
#define HALO_STREAM_ARB_MSTR5_CONFIG_2 0x45058

#define HALO_STREAM_ARB_TX1_CONFIG_0 0x45200
#define HALO_STREAM_ARB_TX1_CONFIG_1 0x45204
#define HALO_STREAM_ARB_TX2_CONFIG_0 0x45208
#define HALO_STREAM_ARB_TX2_CONFIG_1 0x4520C
#define HALO_STREAM_ARB_TX3_CONFIG_0 0x45210
#define HALO_STREAM_ARB_TX3_CONFIG_1 0x45214
#define HALO_STREAM_ARB_TX4_CONFIG_0 0x45218
#define HALO_STREAM_ARB_TX4_CONFIG_1 0x4521C
#define HALO_STREAM_ARB_TX5_CONFIG_0 0x45220
#define HALO_STREAM_ARB_TX5_CONFIG_1 0x45224
#define HALO_STREAM_ARB_TX6_CONFIG_0 0x45228
#define HALO_STREAM_ARB_TX6_CONFIG_1 0x4522C
#define HALO_STREAM_ARB_TX7_CONFIG_0 0x45230
#define HALO_STREAM_ARB_TX7_CONFIG_1 0x45234
#define HALO_STREAM_ARB_TX8_CONFIG_0 0x45238
#define HALO_STREAM_ARB_TX8_CONFIG_1 0x4523C
#define HALO_STREAM_ARB_RX1_CONFIG_0 0x45400
#define HALO_STREAM_ARB_RX1_CONFIG_1 0x45404
#define HALO_STREAM_ARB_RX2_CONFIG_0 0x45408
#define HALO_STREAM_ARB_RX2_CONFIG_1 0x4540C
#define HALO_STREAM_ARB_RX3_CONFIG_0 0x45410
#define HALO_STREAM_ARB_RX3_CONFIG_1 0x45414
#define HALO_STREAM_ARB_RX4_CONFIG_0 0x45418
#define HALO_STREAM_ARB_RX4_CONFIG_1 0x4541C
#define HALO_STREAM_ARB_RX5_CONFIG_0 0x45420
#define HALO_STREAM_ARB_RX5_CONFIG_1 0x45424
#define HALO_STREAM_ARB_RX6_CONFIG_0 0x45428
#define HALO_STREAM_ARB_RX6_CONFIG_1 0x4542C
#define HALO_STREAM_ARB_RX7_CONFIG_0 0x45430
#define HALO_STREAM_ARB_RX7_CONFIG_1 0x45434
#define HALO_STREAM_ARB_RX8_CONFIG_0 0x45438
#define HALO_STREAM_ARB_RX8_CONFIG_1 0x4543C

#define HALO_STREAM_ARB_IRQ0_CONFIG_0 0x45600
#define HALO_STREAM_ARB_IRQ0_CONFIG_1 0x45604
#define HALO_STREAM_ARB_IRQ0_CONFIG_2 0x45608
#define HALO_STREAM_ARB_IRQ1_CONFIG_0 0x45610
#define HALO_STREAM_ARB_IRQ1_CONFIG_1 0x45614
#define HALO_STREAM_ARB_IRQ1_CONFIG_2 0x45618
#define HALO_STREAM_ARB_IRQ2_CONFIG_0 0x45620
#define HALO_STREAM_ARB_IRQ2_CONFIG_1 0x45624
#define HALO_STREAM_ARB_IRQ2_CONFIG_2 0x45628
#define HALO_STREAM_ARB_IRQ3_CONFIG_0 0x45630
#define HALO_STREAM_ARB_IRQ3_CONFIG_1 0x45634
#define HALO_STREAM_ARB_IRQ3_CONFIG_2 0x45638
#define HALO_STREAM_ARB_IRQ4_CONFIG_0 0x45640
#define HALO_STREAM_ARB_IRQ4_CONFIG_1 0x45644
#define HALO_STREAM_ARB_IRQ4_CONFIG_2 0x45648
#define HALO_STREAM_ARB_IRQ5_CONFIG_0 0x45650
#define HALO_STREAM_ARB_IRQ5_CONFIG_1 0x45654
#define HALO_STREAM_ARB_IRQ5_CONFIG_2 0x45658
#define HALO_STREAM_ARB_IRQ6_CONFIG_0 0x45660
#define HALO_STREAM_ARB_IRQ6_CONFIG_1 0x45664
#define HALO_STREAM_ARB_IRQ6_CONFIG_2 0x45668
#define HALO_STREAM_ARB_IRQ7_CONFIG_0 0x45670
#define HALO_STREAM_ARB_IRQ7_CONFIG_1 0x45674
#define HALO_STREAM_ARB_IRQ7_CONFIG_2 0x45678

#define HALO_INTP_CTL_NMI_CONTROL 0x46008

/*
 * HALO_AHBM_WINDOW_DEBUG_1
 */
#define HALO_AHBM_CORE_ERR_ADDR_MASK 0x0fffff00
#define HALO_AHBM_CORE_ERR_ADDR_SHIFT 8
#define HALO_AHBM_ADDR_ERR_MASK 0x00000080
#define HALO_AHBM_LOCKED_ERR_MASK 0x00000040
#define HALO_AHBM_SIZE_ERR_MASK 0x00000020
#define HALO_AHBM_MODE_ERR_MASK 0x00000010
#define HALO_AHBM_AHB_ERR_MASK 0x00000001

/*
 * HALO_SAMPLE_RATE_[RX|TX]n
 */
#define HALO_DSP_RATE_SHIFT 0
#define HALO_DSP_RATE_MASK 0x1f

/*
 * HALO_CCM_CORE_CONTROL
 */
#define HALO_CORE_EN 0x00000001
#define HALO_CORE_EN_MASK 0x00000001
#define HALO_CORE_EN_SHIFT 0
#define HALO_CORE_EN_WIDTH 1
#define HALO_CORE_RESET 0x00000200

/*
 * HALO_MPU_?M_VIO_STATUS
 */
#define HALO_MPU_VIO_STS_MASK 0x007e0000
#define HALO_MPU_VIO_STS_SHIFT 17
#define HALO_MPU_VIO_ERR_MASK 0x00010000
#define HALO_MPU_VIO_ERR_SHIFT 16
#define HALO_MPU_VIO_ERR_WR_MASK 0x00008000
#define HALO_MPU_VIO_ERR_WR_SHIFT 15
#define HALO_MPU_VIO_ERR_SRC_MASK 0x00007fff
#define HALO_MPU_VIO_ERR_SRC_SHIFT 0

#define HALO_MPU_VIO_SRAM 0x01
#define HALO_MPU_VIO_REG 0x02
#define HALO_MPU_VIO_AHB 0x04
#define HALO_MPU_VIO_EREG 0x08
#define HALO_MPU_VIO_EXTERNAL_MEM 0x10
#define HALO_MPU_VIO_NON_EXIST 0x20

/*
 * HALO_STREAM_ARB_MSTRn_CONFIG_0
 */
#define HALO_STREAM_ARB_MSTR_EN_MASK 0x1

/*
 * HALO_STREAM_ARB_[TX|RX]n_CONFIG_0
 * HALO_STREAM_ARB_IRQn_CONFIG_0
 */
#define HALO_STREAM_ARB_MSTR_SEL_DEFAULT 0xfc

static const unsigned int halo_mpu_access[18] = {
	HALO_MPU_WINDOW_ACCESS_0, HALO_MPU_XREG_ACCESS_0,
	HALO_MPU_YREG_ACCESS_0,	  HALO_MPU_XMEM_ACCESS_1,
	HALO_MPU_YMEM_ACCESS_1,	  HALO_MPU_WINDOW_ACCESS_1,
	HALO_MPU_XREG_ACCESS_1,	  HALO_MPU_YREG_ACCESS_1,
	HALO_MPU_XMEM_ACCESS_2,	  HALO_MPU_YMEM_ACCESS_2,
	HALO_MPU_WINDOW_ACCESS_2, HALO_MPU_XREG_ACCESS_2,
	HALO_MPU_YREG_ACCESS_2,	  HALO_MPU_XMEM_ACCESS_3,
	HALO_MPU_YMEM_ACCESS_3,	  HALO_MPU_WINDOW_ACCESS_3,
	HALO_MPU_XREG_ACCESS_3,	  HALO_MPU_YREG_ACCESS_3,
};

struct wm_adsp_buf {
	struct list_head list;
	void *buf;
};

static struct wm_adsp_buf *wm_adsp_buf_alloc(const void *src, size_t len,
					     struct list_head *list)
{
	struct wm_adsp_buf *buf = kzalloc(sizeof(*buf), GFP_KERNEL);

	if (buf == NULL)
		return NULL;

	buf->buf = kmemdup(src, len, GFP_KERNEL | GFP_DMA);
	if (!buf->buf) {
		kfree(buf);
		return NULL;
	}
	memcpy(buf->buf, src, len);

	if (list)
		list_add_tail(&buf->list, list);

	return buf;
}

static void wm_adsp_buf_free(struct list_head *list)
{
	while (!list_empty(list)) {
		struct wm_adsp_buf *buf =
			list_first_entry(list, struct wm_adsp_buf, list);
		list_del(&buf->list);
		kfree(buf->buf);
		kfree(buf);
	}
}

#define WM_ADSP_FW_MBC_VSS 0
#define WM_ADSP_FW_HIFI 1
#define WM_ADSP_FW_TX 2
#define WM_ADSP_FW_TX_SPK 3
#define WM_ADSP_FW_RX 4
#define WM_ADSP_FW_RX_ANC 5
#define WM_ADSP_FW_CTRL 6
#define WM_ADSP_FW_ASR 7
#define WM_ADSP_FW_TRACE 8
#define WM_ADSP_FW_DIAG 10
#define WM_ADSP_FW_CALIB 11

#define WM_ADSP_NUM_FW 12

#define AMBIENT_DEFAULT 30
#define CAL_R_DEFAULT 8392
#define CAL_STATUS_DEFAULT 1

static const char *wm_adsp_fw_text[WM_ADSP_NUM_FW] = {
	[WM_ADSP_FW_MBC_VSS] = "MBC/VSS", [WM_ADSP_FW_HIFI] = "MasterHiFi",
	[WM_ADSP_FW_TX] = "Tx",		  [WM_ADSP_FW_TX_SPK] = "Tx Speaker",
	[WM_ADSP_FW_RX] = "Rx",		  [WM_ADSP_FW_RX_ANC] = "Rx ANC",
	[WM_ADSP_FW_CTRL] = "Voice Ctrl", [WM_ADSP_FW_ASR] = "ASR Assist",
	[WM_ADSP_FW_TRACE] = "Dbg Trace", [WM_ADSP_FW_SPK_PROT] = "Protection",
	[WM_ADSP_FW_DIAG] = "Diag",	  [WM_ADSP_FW_CALIB] = "Diag Z",
};

struct wm_adsp_system_config_xm_hdr {
	__be32 sys_enable;
	__be32 fw_id;
	__be32 fw_rev;
	__be32 boot_status;
	__be32 watchdog;
	__be32 dma_buffer_size;
	__be32 rdma[6];
	__be32 wdma[8];
	__be32 build_job_name[3];
	__be32 build_job_number;
};

struct wm_adsp_alg_xm_struct {
	__be32 magic;
	__be32 smoothing;
	__be32 threshold;
	__be32 host_buf_ptr;
	__be32 start_seq;
	__be32 high_water_mark;
	__be32 low_water_mark;
	__be64 smoothed_power;
};

struct wm_adsp_buffer {
	__be32 X_buf_base; /* XM base addr of first X area */
	__be32 X_buf_size; /* Size of 1st X area in words */
	__be32 X_buf_base2; /* XM base addr of 2nd X area */
	__be32 X_buf_brk; /* Total X size in words */
	__be32 Y_buf_base; /* YM base addr of Y area */
	__be32 wrap; /* Total size X and Y in words */
	__be32 high_water_mark; /* Point at which IRQ is asserted */
	__be32 irq_count; /* bits 1-31 count IRQ assertions */
	__be32 irq_ack; /* acked IRQ count, bit 0 enables IRQ */
	__be32 next_write_index; /* word index of next write */
	__be32 next_read_index; /* word index of next read */
	__be32 error; /* error if any */
	__be32 oldest_block_index; /* word index of oldest surviving */
	__be32 requested_rewind; /* how many blocks rewind was done */
	__be32 reserved_space; /* internal */
	__be32 min_free; /* min free space since stream start */
	__be32 blocks_written[2]; /* total blocks written (64 bit) */
	__be32 words_written[2]; /* total words written (64 bit) */
};

struct wm_adsp_compr;

struct wm_adsp_compr_buf {
	struct wm_adsp *dsp;
	struct wm_adsp_compr *compr;

	struct wm_adsp_buffer_region *regions;
	u32 host_buf_ptr;

	u32 error;
	u32 irq_count;
	int read_index;
	int avail;
};

struct wm_adsp_compr {
	struct wm_adsp *dsp;
	struct wm_adsp_compr_buf *buf;

	struct snd_compr_stream *stream;
	struct snd_compressed_buffer size;

	u32 *raw_buf;
	unsigned int copied_total;

	unsigned int sample_rate;
};

#define WM_ADSP_DATA_WORD_SIZE 3

#define WM_ADSP_MIN_FRAGMENTS 1
#define WM_ADSP_MAX_FRAGMENTS 256
#define WM_ADSP_MIN_FRAGMENT_SIZE (64 * WM_ADSP_DATA_WORD_SIZE)
#define WM_ADSP_MAX_FRAGMENT_SIZE (4096 * WM_ADSP_DATA_WORD_SIZE)

#define WM_ADSP_ALG_XM_STRUCT_MAGIC 0x49aec7

#define HOST_BUFFER_FIELD(field)                                               \
	(offsetof(struct wm_adsp_buffer, field) / sizeof(__be32))

#define ALG_XM_FIELD(field)                                                    \
	(offsetof(struct wm_adsp_alg_xm_struct, field) / sizeof(__be32))

static int wm_adsp_buffer_init(struct wm_adsp *dsp);
static int wm_adsp_buffer_free(struct wm_adsp *dsp);

struct wm_adsp_buffer_region {
	unsigned int offset;
	unsigned int cumulative_size;
	unsigned int mem_type;
	unsigned int base_addr;
};

struct wm_adsp_buffer_region_def {
	unsigned int mem_type;
	unsigned int base_offset;
	unsigned int size_offset;
};

static const struct wm_adsp_buffer_region_def default_regions[] = {
	{
		.mem_type = WMFW_ADSP2_XM,
		.base_offset = HOST_BUFFER_FIELD(X_buf_base),
		.size_offset = HOST_BUFFER_FIELD(X_buf_size),
	},
	{
		.mem_type = WMFW_ADSP2_XM,
		.base_offset = HOST_BUFFER_FIELD(X_buf_base2),
		.size_offset = HOST_BUFFER_FIELD(X_buf_brk),
	},
	{
		.mem_type = WMFW_ADSP2_YM,
		.base_offset = HOST_BUFFER_FIELD(Y_buf_base),
		.size_offset = HOST_BUFFER_FIELD(wrap),
	},
};

struct wm_adsp_fw_caps {
	u32 id;
	struct snd_codec_desc desc;
	int num_regions;
	const struct wm_adsp_buffer_region_def *region_defs;
};

static const struct wm_adsp_fw_caps ctrl_caps[] = {
	{
		.id = SND_AUDIOCODEC_BESPOKE,
		.desc = {
			.max_ch = 1,
			.sample_rates = { 16000 },
			.num_sample_rates = 1,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.num_regions = ARRAY_SIZE(default_regions),
		.region_defs = default_regions,
	},
};

static const struct wm_adsp_fw_caps trace_caps[] = {
	{
		.id = SND_AUDIOCODEC_BESPOKE,
		.desc = {
			.max_ch = 8,
			.sample_rates = {
				4000, 8000, 11025, 12000, 16000, 22050,
				24000, 32000, 44100, 48000, 64000, 88200,
				96000, 176400, 192000
			},
			.num_sample_rates = 15,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.num_regions = ARRAY_SIZE(default_regions),
		.region_defs = default_regions,
	},
};

static const struct {
	const char *file;
	int compr_direction;
	int num_caps;
	const struct wm_adsp_fw_caps *caps;
	bool voice_trigger;
} wm_adsp_fw[WM_ADSP_NUM_FW] = {
	[WM_ADSP_FW_MBC_VSS] =  { .file = "mbc-vss" },
	[WM_ADSP_FW_HIFI] =     { .file = "hifi" },
	[WM_ADSP_FW_TX] =       { .file = "tx" },
	[WM_ADSP_FW_TX_SPK] =   { .file = "tx-spk" },
	[WM_ADSP_FW_RX] =       { .file = "rx" },
	[WM_ADSP_FW_RX_ANC] =   { .file = "rx-anc" },
	[WM_ADSP_FW_CTRL] =     {
		.file = "ctrl",
		.compr_direction = SND_COMPRESS_CAPTURE,
		.num_caps = ARRAY_SIZE(ctrl_caps),
		.caps = ctrl_caps,
		.voice_trigger = true,
	},
	[WM_ADSP_FW_ASR] =      { .file = "asr" },
	[WM_ADSP_FW_TRACE] =    {
		.file = "trace",
		.compr_direction = SND_COMPRESS_CAPTURE,
		.num_caps = ARRAY_SIZE(trace_caps),
		.caps = trace_caps,
	},
	[WM_ADSP_FW_SPK_PROT] = { .file = "spk-prot" },
	[WM_ADSP_FW_DIAG] =     { .file = "diag" },
	[WM_ADSP_FW_CALIB] =     { .file = "diag-z" },
};

struct wm_coeff_ctl_ops {
	int (*xget)(struct snd_kcontrol *kcontrol,
		    struct snd_ctl_elem_value *ucontrol);
	int (*xput)(struct snd_kcontrol *kcontrol,
		    struct snd_ctl_elem_value *ucontrol);
};

struct wm_coeff_ctl {
	const char *name;
	const char *fw_name;
	struct wm_adsp_alg_region alg_region;
	struct wm_coeff_ctl_ops ops;
	struct wm_adsp *dsp;
	unsigned int enabled : 1;
	struct list_head list;
	void *cache;
	unsigned int offset;
	size_t len;
	unsigned int set : 1;
	struct soc_bytes_ext bytes_ext;
	unsigned int flags;
	unsigned int type;
};

static const char *wm_adsp_mem_region_name(unsigned int type)
{
	switch (type) {
	case WMFW_ADSP1_PM:
		return "PM";
	case WMFW_HALO_PM_PACKED:
		return "PM_PACKED";
	case WMFW_ADSP1_DM:
		return "DM";
	case WMFW_ADSP2_XM:
		return "XM";
	case WMFW_HALO_XM_PACKED:
		return "XM_PACKED";
	case WMFW_ADSP2_YM:
		return "YM";
	case WMFW_HALO_YM_PACKED:
		return "YM_PACKED";
	case WMFW_ADSP1_ZM:
		return "ZM";
	default:
		return NULL;
	}
}
static int wm_halo_apply_calibration(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_component *codec = snd_soc_dapm_to_component(w->dapm);
	struct wm_adsp *dsps = snd_soc_component_get_drvdata(codec);
	struct wm_adsp *dsp = &dsps[w->shift];
	int ret;

	switch (dsp->fw) {
	case WM_ADSP_FW_CALIB:
		ret = wm_adsp_k_ctl_put(dsp, "DSP1X Diag Z cd CAL_AMBIENT",
					dsp->ambient);
		if (ret)
			return ret;
		break;
	case WM_ADSP_FW_DIAG:
		ret = wm_adsp_k_ctl_put(dsp, "DSP1X Diag cd CAL_AMBIENT",
					dsp->ambient);
		if (ret)
			return ret;
		break;
	case WM_ADSP_FW_SPK_PROT:
		ret = wm_adsp_k_ctl_put(dsp, "DSP1X Protection cd CAL_R",
					dsp->cal_z);
		if (ret)
			return ret;
		ret = wm_adsp_k_ctl_put(dsp, "DSP1X Protection cd CAL_STATUS",
					dsp->cal_status);
		if (ret)
			return ret;
		ret = wm_adsp_k_ctl_put(dsp, "DSP1X Protection cd CAL_CHECKSUM",
					dsp->cal_chksum);
		if (ret)
			return ret;
		ret = wm_adsp_k_ctl_get(dsp, "DSP1X Protection cd CAL_R");
		if (ret)
			return ret;
		ret = wm_adsp_k_ctl_get(dsp, "DSP1X Protection cd CAL_STATUS");
		if (ret)
			return ret;
		ret = wm_adsp_k_ctl_get(dsp, "DSP1X Protection cd CAL_CHECKSUM");
		if (ret)
			return ret;

		/* Bypass controls are optional across firmware revisions. */
		ret = wm_adsp_k_ctl_put(dsp, "DSP1X Protection cd BYPASS_IN_ENH",
					(dsp->block_bypass ||
					 dsp->block_bypass_in_enh) ?
					0x00400001 : 0x00000000);
		if (ret)
			adsp_dbg(dsp, "No BYPASS_IN_ENH control: %d\n", ret);
		ret = wm_adsp_k_ctl_put(dsp, "DSP1X Protection cd BYPASS_EQ",
					dsp->block_bypass ?
					0x00400001 : 0x00000000);
		if (ret)
			adsp_dbg(dsp, "No BYPASS_EQ control: %d\n", ret);
		ret = wm_adsp_k_ctl_put(dsp, "DSP1X Protection cd BYPASS_ACTI",
					dsp->block_bypass ?
					0x00400001 : 0x00000000);
		if (ret)
			adsp_dbg(dsp, "No BYPASS_ACTI control: %d\n", ret);
		ret = wm_adsp_k_ctl_put(dsp, "DSP1X Protection cd BYPASS_MBL",
					dsp->block_bypass ?
					0x00400001 : 0x00000000);
		if (ret)
			adsp_dbg(dsp, "No BYPASS_MBL control: %d\n", ret);
		break;
	default:
		break;
	}

	adsp_dbg(dsp, "Calibration controls applied for firmware %d\n",
		 dsp->fw);
	return 0;
}

int wm_adsp2_component_probe(struct wm_adsp *dsp, struct snd_soc_component *codec)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(codec);
	char preload[32];

	snprintf(preload, ARRAY_SIZE(preload), "DSP%d Preload", dsp->num);
	snd_soc_dapm_disable_pin(dapm, preload);

	wm_adsp2_init_debugfs(dsp, codec);

	dsp->component = codec;

	snd_soc_add_component_controls(codec, wm_adsp_cal_controls,
				   ARRAY_SIZE(wm_adsp_cal_controls));

	return snd_soc_add_component_controls(
		codec, &wm_adsp_fw_controls[dsp->num - 1], 1);
}

int wm_adsp2_component_remove(struct wm_adsp *dsp, struct snd_soc_component *codec)
{
	wm_adsp2_cleanup_debugfs(dsp);

	return 0;
}

int wm_adsp2_init(struct wm_adsp *dsp)
{
	int ret;

	switch (dsp->rev) {
	case 0:
		/*
		 * Disable the DSP memory by default when in reset for a small
		 * power saving.
		 */
		ret = regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CONTROL,
					 ADSP2_MEM_ENA, 0);
		if (ret) {
			adsp_err(dsp, "Failed to clear memory retention: %d\n",
				 ret);
			return ret;
		}
		break;
	default:
		break;
	}

	INIT_LIST_HEAD(&dsp->alg_regions);
	INIT_LIST_HEAD(&dsp->ctl_list);
	INIT_WORK(&dsp->boot_work, wm_adsp2_boot_work);

	mutex_init(&dsp->pwr_lock);

	return 0;
}

int wm_halo_init(struct wm_adsp *dsp)
{
	INIT_LIST_HEAD(&dsp->alg_regions);
	INIT_LIST_HEAD(&dsp->ctl_list);
	INIT_WORK(&dsp->boot_work, wm_halo_boot_work);

	mutex_init(&dsp->pwr_lock);

	dsp->rx_rate_cache = kcalloc(dsp->n_rx_rates, sizeof(u8), GFP_KERNEL);
	if (!dsp->rx_rate_cache)
		return -ENOMEM;

	dsp->tx_rate_cache = kcalloc(dsp->n_tx_rates, sizeof(u8), GFP_KERNEL);
	if (!dsp->tx_rate_cache) {
		kfree(dsp->rx_rate_cache);
		dsp->rx_rate_cache = NULL;
		return -ENOMEM;
	}

	/* 0x03 is the CS35L41's 48 kHz rate encoding. */
	memset(dsp->rx_rate_cache, 0x03, dsp->n_rx_rates);
	memset(dsp->tx_rate_cache, 0x03, dsp->n_tx_rates);

	dsp->ambient = AMBIENT_DEFAULT;
	dsp->cal_z = CAL_R_DEFAULT;
	dsp->cal_status = CAL_STATUS_DEFAULT;
	dsp->cal_chksum = CAL_R_DEFAULT + CAL_STATUS_DEFAULT;
	return 0;
}

void wm_adsp2_remove(struct wm_adsp *dsp)
{
	struct wm_coeff_ctl *ctl;

	while (!list_empty(&dsp->ctl_list)) {
		ctl = list_first_entry(&dsp->ctl_list, struct wm_coeff_ctl,
				       list);
		list_del(&ctl->list);
		wm_adsp_free_ctl_blk(ctl);
	}

	kfree(dsp->rx_rate_cache);
	dsp->rx_rate_cache = NULL;
	kfree(dsp->tx_rate_cache);
	dsp->tx_rate_cache = NULL;
}

static inline int wm_adsp_compr_attached(struct wm_adsp_compr *compr)
{
	return compr->buf != NULL;
}

static int wm_adsp_compr_attach(struct wm_adsp_compr *compr)
{
	/*
	 * Note this will be more complex once each DSP can support multiple
	 * streams
	 */
	if (!compr->dsp->buffer)
		return -EINVAL;

	compr->buf = compr->dsp->buffer;
	compr->buf->compr = compr;

	return 0;
}

static void wm_adsp_compr_detach(struct wm_adsp_compr *compr)
{
	if (!compr)
		return;

	/* Wake the poll so it can see buffer is no longer attached */
	if (compr->stream)
		snd_compr_fragment_elapsed(compr->stream);

	if (wm_adsp_compr_attached(compr)) {
		compr->buf->compr = NULL;
		compr->buf = NULL;
	}
}

int wm_adsp_compr_open(struct wm_adsp *dsp, struct snd_compr_stream *stream)
{
	struct wm_adsp_compr *compr;
	int ret = 0;

	mutex_lock(&dsp->pwr_lock);

	if (wm_adsp_fw[dsp->fw].num_caps == 0) {
		adsp_err(dsp, "Firmware does not support compressed API\n");
		ret = -ENXIO;
		goto out;
	}

	if (wm_adsp_fw[dsp->fw].compr_direction != stream->direction) {
		adsp_err(dsp, "Firmware does not support stream direction\n");
		ret = -EINVAL;
		goto out;
	}

	if (dsp->compr) {
		/* It is expect this limitation will be removed in future */
		adsp_err(dsp, "Only a single stream supported per DSP\n");
		ret = -EBUSY;
		goto out;
	}

	compr = kzalloc(sizeof(*compr), GFP_KERNEL);
	if (!compr) {
		ret = -ENOMEM;
		goto out;
	}

	compr->dsp = dsp;
	compr->stream = stream;

	dsp->compr = compr;

	stream->runtime->private_data = compr;

out:
	mutex_unlock(&dsp->pwr_lock);

	return ret;
}

int wm_adsp_compr_free(struct snd_compr_stream *stream)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	struct wm_adsp *dsp = compr->dsp;

	mutex_lock(&dsp->pwr_lock);

	wm_adsp_compr_detach(compr);
	dsp->compr = NULL;

	kfree(compr->raw_buf);
	kfree(compr);

	mutex_unlock(&dsp->pwr_lock);

	return 0;
}

static int wm_adsp_compr_check_params(struct snd_compr_stream *stream,
				      struct snd_compr_params *params)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	struct wm_adsp *dsp = compr->dsp;
	const struct wm_adsp_fw_caps *caps;
	const struct snd_codec_desc *desc;
	int i, j;

	if (params->buffer.fragment_size < WM_ADSP_MIN_FRAGMENT_SIZE ||
	    params->buffer.fragment_size > WM_ADSP_MAX_FRAGMENT_SIZE ||
	    params->buffer.fragments < WM_ADSP_MIN_FRAGMENTS ||
	    params->buffer.fragments > WM_ADSP_MAX_FRAGMENTS ||
	    params->buffer.fragment_size % WM_ADSP_DATA_WORD_SIZE) {
		adsp_err(dsp, "Invalid buffer fragsize=%d fragments=%d\n",
			 params->buffer.fragment_size,
			 params->buffer.fragments);

		return -EINVAL;
	}

	for (i = 0; i < wm_adsp_fw[dsp->fw].num_caps; i++) {
		caps = &wm_adsp_fw[dsp->fw].caps[i];
		desc = &caps->desc;

		if (caps->id != params->codec.id)
			continue;

		if (stream->direction == SND_COMPRESS_PLAYBACK) {
			if (desc->max_ch < params->codec.ch_out)
				continue;
		} else {
			if (desc->max_ch < params->codec.ch_in)
				continue;
		}

		if (!(desc->formats & (1 << params->codec.format)))
			continue;

		for (j = 0; j < desc->num_sample_rates; ++j)
			if (desc->sample_rates[j] == params->codec.sample_rate)
				return 0;
	}

	adsp_err(dsp, "Invalid params id=%u ch=%u,%u rate=%u fmt=%u\n",
		 params->codec.id, params->codec.ch_in, params->codec.ch_out,
		 params->codec.sample_rate, params->codec.format);
	return -EINVAL;
}

static inline unsigned int wm_adsp_compr_frag_words(struct wm_adsp_compr *compr)
{
	return compr->size.fragment_size / WM_ADSP_DATA_WORD_SIZE;
}

int wm_adsp_compr_set_params(struct snd_compr_stream *stream,
			     struct snd_compr_params *params)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	unsigned int size;
	int ret;

	ret = wm_adsp_compr_check_params(stream, params);
	if (ret)
		return ret;

	compr->size = params->buffer;

	adsp_dbg(compr->dsp, "fragment_size=%d fragments=%d\n",
		 compr->size.fragment_size, compr->size.fragments);

	size = wm_adsp_compr_frag_words(compr) * sizeof(*compr->raw_buf);
	compr->raw_buf = kmalloc(size, GFP_DMA | GFP_KERNEL);
	if (!compr->raw_buf)
		return -ENOMEM;

	compr->sample_rate = params->codec.sample_rate;

	return 0;
}

int wm_adsp_compr_get_caps(struct snd_compr_stream *stream,
			   struct snd_compr_caps *caps)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	int fw = compr->dsp->fw;
	int i;

	if (wm_adsp_fw[fw].caps) {
		for (i = 0; i < wm_adsp_fw[fw].num_caps; i++)
			caps->codecs[i] = wm_adsp_fw[fw].caps[i].id;

		caps->num_codecs = i;
		caps->direction = wm_adsp_fw[fw].compr_direction;

		caps->min_fragment_size = WM_ADSP_MIN_FRAGMENT_SIZE;
		caps->max_fragment_size = WM_ADSP_MAX_FRAGMENT_SIZE;
		caps->min_fragments = WM_ADSP_MIN_FRAGMENTS;
		caps->max_fragments = WM_ADSP_MAX_FRAGMENTS;
	}

	return 0;
}

static int wm_adsp_read_data_block(struct wm_adsp *dsp, int mem_type,
				   unsigned int mem_addr,
				   unsigned int num_words, u32 *data)
{
	struct wm_adsp_region const *mem = wm_adsp_find_region(dsp, mem_type);
	unsigned int i, reg;
	int ret;

	if (!mem)
		return -EINVAL;

	reg = wm_adsp_region_to_reg(dsp, mem, mem_addr);

	ret = wm_adsp2_raw_read(MAXBULK, dsp->regmap, reg, data,
				sizeof(*data) * num_words);
	if (ret < 0)
		return ret;

	for (i = 0; i < num_words; ++i)
		data[i] = be32_to_cpu(data[i]) & 0x00ffffffu;

	return 0;
}

static inline int wm_adsp_read_data_word(struct wm_adsp *dsp, int mem_type,
					 unsigned int mem_addr, u32 *data)
{
	return wm_adsp_read_data_block(dsp, mem_type, mem_addr, 1, data);
}

static int wm_adsp_write_data_word(struct wm_adsp *dsp, int mem_type,
				   unsigned int mem_addr, u32 data)
{
	struct wm_adsp_region const *mem = wm_adsp_find_region(dsp, mem_type);
	unsigned int reg;

	if (!mem)
		return -EINVAL;

	reg = wm_adsp_region_to_reg(dsp, mem, mem_addr);

	data = cpu_to_be32(data & 0x00ffffffu);

	return regmap_raw_write(dsp->regmap, reg, &data, sizeof(data));
}

static inline int wm_adsp_buffer_read(struct wm_adsp_compr_buf *buf,
				      unsigned int field_offset, u32 *data)
{
	return wm_adsp_read_data_word(buf->dsp, WMFW_ADSP2_XM,
				      buf->host_buf_ptr + field_offset, data);
}

static inline int wm_adsp_buffer_write(struct wm_adsp_compr_buf *buf,
				       unsigned int field_offset, u32 data)
{
	return wm_adsp_write_data_word(buf->dsp, WMFW_ADSP2_XM,
				       buf->host_buf_ptr + field_offset, data);
}

static int wm_adsp_buffer_locate(struct wm_adsp_compr_buf *buf)
{
	struct wm_adsp_alg_region *alg_region;
	struct wm_adsp *dsp = buf->dsp;
	u32 xmalg, addr, magic;
	int i, ret;

	alg_region = wm_adsp_find_alg_region(dsp, WMFW_ADSP2_XM, dsp->fw_id);
	xmalg = sizeof(struct wm_adsp_system_config_xm_hdr) / sizeof(__be32);

	addr = alg_region->base + xmalg + ALG_XM_FIELD(magic);
	ret = wm_adsp_read_data_word(dsp, WMFW_ADSP2_XM, addr, &magic);
	if (ret < 0)
		return ret;

	if (magic != WM_ADSP_ALG_XM_STRUCT_MAGIC)
		return -EINVAL;

	addr = alg_region->base + xmalg + ALG_XM_FIELD(host_buf_ptr);
	for (i = 0; i < 5; ++i) {
		ret = wm_adsp_read_data_word(dsp, WMFW_ADSP2_XM, addr,
					     &buf->host_buf_ptr);
		if (ret < 0)
			return ret;

		if (buf->host_buf_ptr)
			break;

		usleep_range(1000, 2000);
	}

	if (!buf->host_buf_ptr)
		return -EIO;

	adsp_dbg(dsp, "host_buf_ptr=%x\n", buf->host_buf_ptr);

	return 0;
}

static int wm_adsp_buffer_populate(struct wm_adsp_compr_buf *buf)
{
	const struct wm_adsp_fw_caps *caps = wm_adsp_fw[buf->dsp->fw].caps;
	struct wm_adsp_buffer_region *region;
	u32 offset = 0;
	int i, ret;

	for (i = 0; i < caps->num_regions; ++i) {
		region = &buf->regions[i];

		region->offset = offset;
		region->mem_type = caps->region_defs[i].mem_type;

		ret = wm_adsp_buffer_read(buf, caps->region_defs[i].base_offset,
					  &region->base_addr);
		if (ret < 0)
			return ret;

		ret = wm_adsp_buffer_read(buf, caps->region_defs[i].size_offset,
					  &offset);
		if (ret < 0)
			return ret;

		region->cumulative_size = offset;

		adsp_dbg(buf->dsp,
			 "region=%d type=%d base=%04x off=%04x size=%04x\n", i,
			 region->mem_type, region->base_addr, region->offset,
			 region->cumulative_size);
	}

	return 0;
}

static int wm_adsp_buffer_init(struct wm_adsp *dsp)
{
	struct wm_adsp_compr_buf *buf;
	int ret;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf->dsp = dsp;
	buf->read_index = -1;
	buf->irq_count = 0xFFFFFFFF;

	ret = wm_adsp_buffer_locate(buf);
	if (ret < 0) {
		adsp_err(dsp, "Failed to acquire host buffer: %d\n", ret);
		goto err_buffer;
	}

	buf->regions = kcalloc(wm_adsp_fw[dsp->fw].caps->num_regions,
			       sizeof(*buf->regions), GFP_KERNEL);
	if (!buf->regions) {
		ret = -ENOMEM;
		goto err_buffer;
	}

	ret = wm_adsp_buffer_populate(buf);
	if (ret < 0) {
		adsp_err(dsp, "Failed to populate host buffer: %d\n", ret);
		goto err_regions;
	}

	dsp->buffer = buf;

	return 0;

err_regions:
	kfree(buf->regions);
err_buffer:
	kfree(buf);
	return ret;
}

static int wm_adsp_buffer_free(struct wm_adsp *dsp)
{
	if (dsp->buffer) {
		wm_adsp_compr_detach(dsp->buffer->compr);

		kfree(dsp->buffer->regions);
		kfree(dsp->buffer);

		dsp->buffer = NULL;
	}

	return 0;
}

int wm_adsp_compr_trigger(struct snd_compr_stream *stream, int cmd)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	struct wm_adsp *dsp = compr->dsp;
	int ret = 0;

	adsp_dbg(dsp, "Trigger: %d\n", cmd);

	mutex_lock(&dsp->pwr_lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (wm_adsp_compr_attached(compr))
			break;

		ret = wm_adsp_compr_attach(compr);
		if (ret < 0) {
			adsp_err(dsp, "Failed to link buffer and stream: %d\n",
				 ret);
			break;
		}

		/* Trigger the IRQ at one fragment of data */
		ret = wm_adsp_buffer_write(compr->buf,
					   HOST_BUFFER_FIELD(high_water_mark),
					   wm_adsp_compr_frag_words(compr));
		if (ret < 0) {
			adsp_err(dsp, "Failed to set high water mark: %d\n",
				 ret);
			break;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&dsp->pwr_lock);

	return ret;
}

static inline int wm_adsp_buffer_size(struct wm_adsp_compr_buf *buf)
{
	int last_region = wm_adsp_fw[buf->dsp->fw].caps->num_regions - 1;

	return buf->regions[last_region].cumulative_size;
}

static int wm_adsp_buffer_update_avail(struct wm_adsp_compr_buf *buf)
{
	u32 next_read_index, next_write_index;
	int write_index, read_index, avail;
	int ret;

	/* Only sync read index if we haven't already read a valid index */
	if (buf->read_index < 0) {
		ret = wm_adsp_buffer_read(buf,
					  HOST_BUFFER_FIELD(next_read_index),
					  &next_read_index);
		if (ret < 0)
			return ret;

		read_index = sign_extend32(next_read_index, 23);

		if (read_index < 0) {
			adsp_dbg(buf->dsp, "Avail check on unstarted stream\n");
			return 0;
		}

		buf->read_index = read_index;
	}

	ret = wm_adsp_buffer_read(buf, HOST_BUFFER_FIELD(next_write_index),
				  &next_write_index);
	if (ret < 0)
		return ret;

	write_index = sign_extend32(next_write_index, 23);

	avail = write_index - buf->read_index;
	if (avail < 0)
		avail += wm_adsp_buffer_size(buf);

	adsp_dbg(buf->dsp, "readindex=0x%x, writeindex=0x%x, avail=%d\n",
		 buf->read_index, write_index, avail * WM_ADSP_DATA_WORD_SIZE);

	buf->avail = avail;

	return 0;
}

static int wm_adsp_buffer_get_error(struct wm_adsp_compr_buf *buf)
{
	int ret;

	ret = wm_adsp_buffer_read(buf, HOST_BUFFER_FIELD(error), &buf->error);
	if (ret < 0) {
		adsp_err(buf->dsp, "Failed to check buffer error: %d\n", ret);
		return ret;
	}
	if (buf->error != 0) {
		adsp_err(buf->dsp, "Buffer error occurred: %d\n", buf->error);
		return -EIO;
	}

	return 0;
}

int wm_adsp_compr_handle_irq(struct wm_adsp *dsp)
{
	struct wm_adsp_compr_buf *buf;
	struct wm_adsp_compr *compr;
	int ret = 0;

	mutex_lock(&dsp->pwr_lock);

	buf = dsp->buffer;
	compr = dsp->compr;

	if (!buf) {
		ret = -ENODEV;
		goto out;
	}

	adsp_dbg(dsp, "Handling buffer IRQ\n");

	ret = wm_adsp_buffer_get_error(buf);
	if (ret < 0)
		goto out_notify; /* Wake poll to report error */

	ret = wm_adsp_buffer_read(buf, HOST_BUFFER_FIELD(irq_count),
				  &buf->irq_count);
	if (ret < 0) {
		adsp_err(dsp, "Failed to get irq_count: %d\n", ret);
		goto out;
	}

	ret = wm_adsp_buffer_update_avail(buf);
	if (ret < 0) {
		adsp_err(dsp, "Error reading avail: %d\n", ret);
		goto out;
	}

	if (wm_adsp_fw[dsp->fw].voice_trigger && buf->irq_count == 2)
		ret = WM_ADSP_COMPR_VOICE_TRIGGER;

out_notify:
	if (compr && compr->stream)
		snd_compr_fragment_elapsed(compr->stream);

out:
	mutex_unlock(&dsp->pwr_lock);

	return ret;
}

static int wm_adsp_buffer_reenable_irq(struct wm_adsp_compr_buf *buf)
{
	if (buf->irq_count & 0x01)
		return 0;

	adsp_dbg(buf->dsp, "Enable IRQ(0x%x) for next fragment\n",
		 buf->irq_count);

	buf->irq_count |= 0x01;

	return wm_adsp_buffer_write(buf, HOST_BUFFER_FIELD(irq_ack),
				    buf->irq_count);
}

int wm_adsp_compr_pointer(struct snd_compr_stream *stream,
			  struct snd_compr_tstamp *tstamp)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	struct wm_adsp *dsp = compr->dsp;
	struct wm_adsp_compr_buf *buf;
	int ret = 0;

	adsp_dbg(dsp, "Pointer request\n");

	mutex_lock(&dsp->pwr_lock);

	buf = compr->buf;

	if (!compr->buf || compr->buf->error) {
		snd_compr_stop_error(stream, SNDRV_PCM_STATE_XRUN);
		ret = -EIO;
		goto out;
	}

	if (buf->avail < wm_adsp_compr_frag_words(compr)) {
		ret = wm_adsp_buffer_update_avail(buf);
		if (ret < 0) {
			adsp_err(dsp, "Error reading avail: %d\n", ret);
			goto out;
		}

		/*
		 * If we really have less than 1 fragment available tell the
		 * DSP to inform us once a whole fragment is available.
		 */
		if (buf->avail < wm_adsp_compr_frag_words(compr)) {
			ret = wm_adsp_buffer_get_error(buf);
			if (ret < 0) {
				if (compr->buf->error)
					snd_compr_stop_error(
						stream, SNDRV_PCM_STATE_XRUN);
				goto out;
			}

			ret = wm_adsp_buffer_reenable_irq(buf);
			if (ret < 0) {
				adsp_err(dsp,
					 "Failed to re-enable buffer IRQ: %d\n",
					 ret);
				goto out;
			}
		}
	}

	tstamp->copied_total = compr->copied_total;
	tstamp->copied_total += buf->avail * WM_ADSP_DATA_WORD_SIZE;
	tstamp->sampling_rate = compr->sample_rate;

out:
	mutex_unlock(&dsp->pwr_lock);

	return ret;
}

static int wm_adsp_buffer_capture_block(struct wm_adsp_compr *compr, int target)
{
	struct wm_adsp_compr_buf *buf = compr->buf;
	u8 *pack_in = (u8 *)compr->raw_buf;
	u8 *pack_out = (u8 *)compr->raw_buf;
	unsigned int adsp_addr;
	int mem_type, nwords, max_read;
	int i, j, ret;

	/* Calculate read parameters */
	for (i = 0; i < wm_adsp_fw[buf->dsp->fw].caps->num_regions; ++i)
		if (buf->read_index < buf->regions[i].cumulative_size)
			break;

	if (i == wm_adsp_fw[buf->dsp->fw].caps->num_regions)
		return -EINVAL;

	mem_type = buf->regions[i].mem_type;
	adsp_addr = buf->regions[i].base_addr +
		    (buf->read_index - buf->regions[i].offset);

	max_read = wm_adsp_compr_frag_words(compr);
	nwords = buf->regions[i].cumulative_size - buf->read_index;

	if (nwords > target)
		nwords = target;
	if (nwords > buf->avail)
		nwords = buf->avail;
	if (nwords > max_read)
		nwords = max_read;
	if (!nwords)
		return 0;

	/* Read data from DSP */
	ret = wm_adsp_read_data_block(buf->dsp, mem_type, adsp_addr, nwords,
				      compr->raw_buf);
	if (ret < 0)
		return ret;

	/* Remove the padding bytes from the data read from the DSP */
	for (i = 0; i < nwords; i++) {
		for (j = 0; j < WM_ADSP_DATA_WORD_SIZE; j++)
			*pack_out++ = *pack_in++;

		pack_in += sizeof(*(compr->raw_buf)) - WM_ADSP_DATA_WORD_SIZE;
	}

	/* update read index to account for words read */
	buf->read_index += nwords;
	if (buf->read_index == wm_adsp_buffer_size(buf))
		buf->read_index = 0;

	ret = wm_adsp_buffer_write(buf, HOST_BUFFER_FIELD(next_read_index),
				   buf->read_index);
	if (ret < 0)
		return ret;

	/* update avail to account for words read */
	buf->avail -= nwords;

	return nwords;
}

static int wm_adsp_compr_read(struct wm_adsp_compr *compr, char __user *buf,
			      size_t count)
{
	struct wm_adsp *dsp = compr->dsp;
	int ntotal = 0;
	int nwords, nbytes;

	adsp_dbg(dsp, "Requested read of %zu bytes\n", count);

	if (!compr->buf || compr->buf->error) {
		snd_compr_stop_error(compr->stream, SNDRV_PCM_STATE_XRUN);
		return -EIO;
	}

	count /= WM_ADSP_DATA_WORD_SIZE;

	do {
		nwords = wm_adsp_buffer_capture_block(compr, count);
		if (nwords < 0) {
			adsp_err(dsp, "Failed to capture block: %d\n", nwords);
			return nwords;
		}

		nbytes = nwords * WM_ADSP_DATA_WORD_SIZE;

		adsp_dbg(dsp, "Read %d bytes\n", nbytes);

		if (copy_to_user(buf + ntotal, compr->raw_buf, nbytes)) {
			adsp_err(dsp, "Failed to copy data to user: %d, %d\n",
				 ntotal, nbytes);
			return -EFAULT;
		}

		count -= nwords;
		ntotal += nbytes;
	} while (nwords > 0 && count > 0);

	compr->copied_total += ntotal;

	return ntotal;
}

int wm_adsp_compr_copy(struct snd_compr_stream *stream, char __user *buf,
		       size_t count)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	struct wm_adsp *dsp = compr->dsp;
	int ret;

	mutex_lock(&dsp->pwr_lock);

	if (stream->direction == SND_COMPRESS_CAPTURE)
		ret = wm_adsp_compr_read(compr, buf, count);
	else
		ret = -ENOTSUPP;

	mutex_unlock(&dsp->pwr_lock);

	return ret;
}

int wm_adsp2_lock(struct wm_adsp *dsp, unsigned int lock_regions)
{
	struct regmap *regmap = dsp->regmap;
	unsigned int code0, code1, lock_reg;

	if (!(lock_regions & WM_ADSP2_REGION_ALL))
		return 0;

	lock_regions &= WM_ADSP2_REGION_ALL;
	lock_reg = dsp->base + ADSP2_LOCK_REGION_1_LOCK_REGION_0;

	while (lock_regions) {
		code0 = code1 = 0;
		if (lock_regions & BIT(0)) {
			code0 = ADSP2_LOCK_CODE_0;
			code1 = ADSP2_LOCK_CODE_1;
		}
		if (lock_regions & BIT(1)) {
			code0 |= ADSP2_LOCK_CODE_0 << ADSP2_LOCK_REGION_SHIFT;
			code1 |= ADSP2_LOCK_CODE_1 << ADSP2_LOCK_REGION_SHIFT;
		}
		regmap_write(regmap, lock_reg, code0);
		regmap_write(regmap, lock_reg, code1);
		lock_regions >>= 2;
		lock_reg += 2;
	}

	return 0;
}

irqreturn_t wm_adsp2_bus_error(struct wm_adsp *dsp)
{
	unsigned int val;
	struct regmap *regmap = dsp->regmap;
	int ret = 0;

	ret = regmap_read(regmap, dsp->base + ADSP2_LOCK_REGION_CTRL, &val);
	if (ret) {
		adsp_err(dsp, "Failed to read Region Lock Ctrl register: %d\n",
			 ret);
		return IRQ_HANDLED;
	}

	if (val & ADSP2_WDT_TIMEOUT_STS_MASK) {
		adsp_err(dsp, "watchdog timeout error\n");
		wm_adsp_stop_watchdog(dsp);
	}

	if (val & (ADSP2_SLAVE_ERR_MASK | ADSP2_REGION_LOCK_ERR_MASK)) {
		if (val & ADSP2_SLAVE_ERR_MASK)
			adsp_err(dsp, "bus error: slave error\n");
		else
			adsp_err(dsp, "bus error: region lock error\n");

		ret = regmap_read(regmap, dsp->base + ADSP2_BUS_ERR_ADDR, &val);
		if (ret) {
			adsp_err(dsp,
				 "Failed to read Bus Err Addr register: %d\n",
				 ret);
			return IRQ_HANDLED;
		}

		adsp_err(dsp, "bus error address = 0x%x\n",
			 val & ADSP2_BUS_ERR_ADDR_MASK);

		ret = regmap_read(regmap,
				  dsp->base + ADSP2_PMEM_ERR_ADDR_XMEM_ERR_ADDR,
				  &val);
		if (ret) {
			adsp_err(
				dsp,
				"Failed to read Pmem Xmem Err Addr register: %d\n",
				ret);
			return IRQ_HANDLED;
		}

		adsp_err(dsp, "xmem error address = 0x%x\n",
			 val & ADSP2_XMEM_ERR_ADDR_MASK);
		adsp_err(dsp, "pmem error address = 0x%x\n",
			 (val & ADSP2_PMEM_ERR_ADDR_MASK) >>
				 ADSP2_PMEM_ERR_ADDR_SHIFT);
	}

	regmap_update_bits(regmap, dsp->base + ADSP2_LOCK_REGION_CTRL,
			   ADSP2_CTRL_ERR_EINT, ADSP2_CTRL_ERR_EINT);

	return IRQ_HANDLED;
}

static void wm_halo_dump_fault_info(struct wm_adsp *dsp, const char *region,
				    unsigned int addr, unsigned int status)
{
	unsigned int write = status & HALO_MPU_VIO_ERR_WR_MASK;
	unsigned int type =
		(status & HALO_MPU_VIO_STS_MASK) >> HALO_MPU_VIO_STS_SHIFT;
	unsigned int src = (status & HALO_MPU_VIO_ERR_SRC_MASK) >>
			   HALO_MPU_VIO_ERR_SRC_SHIFT;

	adsp_warn(dsp, "%s: FAULT_ADDR:0x%x FAULT_STATUS:0x%x %s\n", region,
		  addr, status, write ? "write" : "read");

	switch (src) {
	case 0:
		adsp_warn(dsp, "%s: SRC=HALO\n", region);
		break;
	default:
		adsp_warn(dsp, "%s: SRC=Requestor%u\n", region, src);
		break;
	}

	adsp_warn(dsp, "%s: %s %s %s %s %s %s\n", region,
		  type & HALO_MPU_VIO_SRAM ? "SRAM" : "",
		  type & HALO_MPU_VIO_REG ? "REG" : "",
		  type & HALO_MPU_VIO_AHB ? "AHB" : "",
		  type & HALO_MPU_VIO_EREG ? "EREG" : "",
		  type & HALO_MPU_VIO_EXTERNAL_MEM ? "ExtMem" : "",
		  type & HALO_MPU_VIO_NON_EXIST ? "NotExist" : "");
}

irqreturn_t wm_halo_bus_error(struct wm_adsp *dsp)
{
	struct regmap *regmap = dsp->regmap;
	unsigned int fault[6], ahb_sts, reg;
	int ret;

	mutex_lock(&dsp->pwr_lock);

	/* Ensure we log the fault even if we fail to read the fault info */
	adsp_warn(dsp, "MPU FAULT\n");

	ret = regmap_read(regmap, dsp->base_sysinfo + HALO_AHBM_WINDOW_DEBUG_1,
			  &ahb_sts);
	if (ret) {
		adsp_warn(dsp, "Failed to read AHB DEBUG_1 (%d)\n", ret);
		goto exit_unlock;
	}

	adsp_warn(dsp, "AHB WINDOW: ADDR: 0x%x STATUS: 0x%x\n",
		  (ahb_sts & HALO_AHBM_CORE_ERR_ADDR_MASK) >>
			  HALO_AHBM_CORE_ERR_ADDR_SHIFT,
		  ahb_sts);
	adsp_warn(dsp, "AHB WINDOW: %s %s %s %s\n",
		  (ahb_sts & HALO_AHBM_ADDR_ERR_MASK) ? "ADDR" : "",
		  (ahb_sts & HALO_AHBM_LOCKED_ERR_MASK) ? "LOCKED" : "",
		  (ahb_sts & HALO_AHBM_SIZE_ERR_MASK) ? "SIZE" : "",
		  (ahb_sts & HALO_AHBM_MODE_ERR_MASK) ? "MODE" : "");

	ret = regmap_read(regmap, dsp->base_sysinfo + HALO_AHBM_WINDOW_DEBUG_0,
			  &ahb_sts);
	if (ret) {
		adsp_warn(dsp, "Failed to read AHB DEBUG_0 (%d)\n", ret);
		goto exit_unlock;
	}

	adsp_warn(dsp, "AHB SYS_ADDR: 0x%x\n", ahb_sts);

	ret = regmap_bulk_read(regmap, dsp->base + HALO_MPU_XM_VIO_ADDR, fault,
			       ARRAY_SIZE(fault));
	if (ret) {
		adsp_warn(dsp, "Failed to read MPU fault info (%d)\n", ret);
		goto exit_unlock;
	}

	wm_halo_dump_fault_info(dsp, "XM", fault[0], fault[1]);
	wm_halo_dump_fault_info(dsp, "YM", fault[2], fault[3]);
	wm_halo_dump_fault_info(dsp, "PM", fault[4], fault[5]);

	/* Clear fault status */
	for (reg = HALO_MPU_XM_VIO_STATUS; reg <= HALO_MPU_PM_VIO_STATUS;
	     reg += 8) {
		ret = regmap_write(regmap, dsp->base + reg, 0);
		if (ret)
			adsp_warn(dsp,
				  "Failed to clear MPU status @0x%x (%d)\n",
				  reg, ret);
	}

exit_unlock:
	mutex_unlock(&dsp->pwr_lock);

	return IRQ_HANDLED;
}

