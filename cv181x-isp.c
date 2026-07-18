// SPDX-License-Identifier: GPL-2.0-only
/* Minimal Sophgo CV181x ISP frontend support for bypass capture */

#include <linux/io.h>
#include <linux/module.h>

#include "cv181x-camera.h"

#define CV181X_PRE_RAW_FE0_FRAME_VLD	0x00028
#define CV181X_ISPTOP_BASE		0x70000
#define CV181X_ISPTOP_INT_EVENT0	(CV181X_ISPTOP_BASE + 0x00)
#define CV181X_ISPTOP_INT_EVENT0_EN	(CV181X_ISPTOP_BASE + 0x10)
#define CV181X_ISPTOP_SW_CTRL0		(CV181X_ISPTOP_BASE + 0x20)
#define CV181X_ISPTOP_SW_CTRL1		(CV181X_ISPTOP_BASE + 0x24)
#define CV181X_ISPTOP_SCENARIOS_CTRL	(CV181X_ISPTOP_BASE + 0x30)

#define CV181X_ISPTOP_FRAME_DONE_FE0_CH0	BIT(0)
#define CV181X_PRE_RAW_FE_FRAME_VLD_CH0		BIT(0)
#define CV181X_PRE_RAW_FE_PQ_VLD_CH0		BIT(4)
#define CV181X_ISPTOP_SW0_SHAW_UP_FE0		BIT(16)
#define CV181X_ISPTOP_SW0_SHAW_UP_BE		BIT(24)
#define CV181X_ISPTOP_SW0_SHAW_UP_RAW		BIT(26)
#define CV181X_ISPTOP_SW0_SHAW_UP_POST		BIT(27)
#define CV181X_ISPTOP_SW1_PQ_UP_FE0		BIT(0)
#define CV181X_ISPTOP_SW1_PQ_UP_BE		BIT(8)
#define CV181X_ISPTOP_SW1_PQ_UP_RAW		BIT(10)
#define CV181X_ISPTOP_SW1_PQ_UP_POST		BIT(11)
#define CV181X_ISPTOP_SCEN_RAW2YUV_422_ENABLE	BIT(16)
#define CV181X_ISPTOP_SCEN_DCI_RGB0YUV1		BIT(21)

static void cv181x_isp_update_bits(void __iomem *base, u32 offset,
				   u32 mask, u32 value)
{
	u32 reg = readl(base + offset);

	reg &= ~mask;
	reg |= value & mask;
	writel(reg, base + offset);
}

void cv181x_isp_start(struct cv181x_camera_dev *cam)
{
	u32 scenario;

	scenario = readl(cam->vi + CV181X_ISPTOP_SCENARIOS_CTRL);
	scenario |= CV181X_ISPTOP_SCEN_RAW2YUV_422_ENABLE |
		    CV181X_ISPTOP_SCEN_DCI_RGB0YUV1;
	writel(scenario, cam->vi + CV181X_ISPTOP_SCENARIOS_CTRL);

	writel(CV181X_ISPTOP_SW1_PQ_UP_FE0 |
	       CV181X_ISPTOP_SW1_PQ_UP_BE |
	       CV181X_ISPTOP_SW1_PQ_UP_RAW |
	       CV181X_ISPTOP_SW1_PQ_UP_POST,
	       cam->vi + CV181X_ISPTOP_SW_CTRL1);
	writel(CV181X_ISPTOP_SW0_SHAW_UP_FE0 |
	       CV181X_ISPTOP_SW0_SHAW_UP_BE |
	       CV181X_ISPTOP_SW0_SHAW_UP_RAW |
	       CV181X_ISPTOP_SW0_SHAW_UP_POST,
	       cam->vi + CV181X_ISPTOP_SW_CTRL0);

	writel(U32_MAX, cam->vi + CV181X_ISPTOP_INT_EVENT0);
	cv181x_isp_update_bits(cam->vi, CV181X_ISPTOP_INT_EVENT0_EN,
			       CV181X_ISPTOP_FRAME_DONE_FE0_CH0,
			       CV181X_ISPTOP_FRAME_DONE_FE0_CH0);
}
EXPORT_SYMBOL_GPL(cv181x_isp_start);

void cv181x_isp_stop(struct cv181x_camera_dev *cam)
{
	cv181x_isp_update_bits(cam->vi, CV181X_ISPTOP_INT_EVENT0_EN,
			       CV181X_ISPTOP_FRAME_DONE_FE0_CH0, 0);
	cv181x_isp_update_bits(cam->vi, CV181X_PRE_RAW_FE0_FRAME_VLD,
			       CV181X_PRE_RAW_FE_FRAME_VLD_CH0 |
			       CV181X_PRE_RAW_FE_PQ_VLD_CH0, 0);
	writel(U32_MAX, cam->vi + CV181X_ISPTOP_INT_EVENT0);
}
EXPORT_SYMBOL_GPL(cv181x_isp_stop);

void cv181x_isp_trigger(struct cv181x_camera_dev *cam)
{
	cv181x_isp_update_bits(cam->vi, CV181X_PRE_RAW_FE0_FRAME_VLD,
			       CV181X_PRE_RAW_FE_FRAME_VLD_CH0 |
			       CV181X_PRE_RAW_FE_PQ_VLD_CH0,
			       CV181X_PRE_RAW_FE_FRAME_VLD_CH0 |
			       CV181X_PRE_RAW_FE_PQ_VLD_CH0);
}
EXPORT_SYMBOL_GPL(cv181x_isp_trigger);

u32 cv181x_isp_irq_status(struct cv181x_camera_dev *cam)
{
	u32 status = readl(cam->vi + CV181X_ISPTOP_INT_EVENT0);

	writel(status, cam->vi + CV181X_ISPTOP_INT_EVENT0);
	return status & CV181X_ISPTOP_FRAME_DONE_FE0_CH0;
}
EXPORT_SYMBOL_GPL(cv181x_isp_irq_status);

MODULE_DESCRIPTION("Sophgo CV181x ISP bypass frontend support");
MODULE_LICENSE("GPL");
