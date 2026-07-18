// SPDX-License-Identifier: GPL-2.0-only
/* Sophgo CV181x video-input bridge and capture DMA */

#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/module.h>

#include <media/mipi-csi2.h>

#include "cv181x-camera.h"

#define CV181X_VI_CSIBDG0		0x0800
#define CV181X_VI_DMA_CTL6		0x0b00
#define CV181X_CSIBDG_TOP_CTRL		(CV181X_VI_CSIBDG0 + 0x00)
#define CV181X_CSIBDG_INT_CTRL		(CV181X_VI_CSIBDG0 + 0x04)
#define CV181X_CSIBDG_DPCM_MODE		(CV181X_VI_CSIBDG0 + 0x08)
#define CV181X_CSIBDG_LD_DPCM_MODE	(CV181X_VI_CSIBDG0 + 0x0c)
#define CV181X_CSIBDG_CH0_SIZE		(CV181X_VI_CSIBDG0 + 0x10)
#define CV181X_CSIBDG_INT_STATUS0	(CV181X_VI_CSIBDG0 + 0xe0)
#define CV181X_CSIBDG_INT_STATUS1	(CV181X_VI_CSIBDG0 + 0xe4)

#define CV181X_DMA_SYS_CONTROL		(CV181X_VI_DMA_CTL6 + 0x00)
#define CV181X_DMA_BASE_ADDR		(CV181X_VI_DMA_CTL6 + 0x04)
#define CV181X_DMA_SEGLEN		(CV181X_VI_DMA_CTL6 + 0x08)
#define CV181X_DMA_STRIDE		(CV181X_VI_DMA_CTL6 + 0x0c)
#define CV181X_DMA_SEGNUM		(CV181X_VI_DMA_CTL6 + 0x10)

#define CV181X_CSIBDG_TOP_CSI_MODE_MASK		GENMASK(1, 0)
#define CV181X_CSIBDG_TOP_CSI_IN_FORMAT		BIT(2)
#define CV181X_CSIBDG_TOP_CH0_DMA_WR_ENABLE	BIT(6)
#define CV181X_CSIBDG_TOP_YUV_PACK_MODE		BIT(20)
#define CV181X_CSIBDG_TOP_CSI_ENABLE		BIT(24)
#define CV181X_CSIBDG_TOP_SHDW_READ_SEL		BIT(28)
#define CV181X_CSIBDG_TOP_CSI_UP_REG		BIT(31)
#define CV181X_CSIBDG_INT_CH0_VS		BIT(0)
#define CV181X_CSIBDG_INT_CH0_SIZE_ERROR	BIT(3)
#define CV181X_CSIBDG_INT_DMA_ERROR		BIT(29)
#define CV181X_CSIBDG_INT_FIFO_OVERFLOW		BIT(31)
#define CV181X_CSIBDG_STATUS0_CH0_ERRORS	GENMASK(7, 4)
#define CV181X_CSIBDG_STATUS1_ERRORS		(BIT(0) | BIT(1) | BIT(4))

#define CV181X_DMA_SYS_BASEH_MASK		GENMASK(15, 8)
#define CV181X_DMA_SYS_BASE_SEL			BIT(16)
#define CV181X_DMA_SYS_STRIDE_SEL		BIT(17)
#define CV181X_DMA_SYS_SEGLEN_SEL		BIT(18)
#define CV181X_DMA_SYS_SEGNUM_SEL		BIT(19)
#define CV181X_DMA_SYS_UPDATE_BASE_ADDR		BIT(21)

void cv181x_vi_dma_set_addr(struct cv181x_camera_dev *cam, dma_addr_t addr)
{
	u32 sys;

	writel(lower_32_bits(addr), cam->vi + CV181X_DMA_BASE_ADDR);
	sys = readl(cam->vi + CV181X_DMA_SYS_CONTROL);
	sys &= ~CV181X_DMA_SYS_BASEH_MASK;
	sys |= FIELD_PREP(CV181X_DMA_SYS_BASEH_MASK, upper_32_bits(addr));
	sys |= CV181X_DMA_SYS_BASE_SEL | CV181X_DMA_SYS_UPDATE_BASE_ADDR;
	writel(sys, cam->vi + CV181X_DMA_SYS_CONTROL);
}
EXPORT_SYMBOL_GPL(cv181x_vi_dma_set_addr);

void cv181x_vi_dma_configure(struct cv181x_camera_dev *cam, dma_addr_t addr)
{
	u32 line = ALIGN(cam->pix.bytesperline, 16);
	u32 sys;

	writel(line, cam->vi + CV181X_DMA_SEGLEN);
	writel(cam->pix.bytesperline, cam->vi + CV181X_DMA_STRIDE);
	writel(cam->pix.height, cam->vi + CV181X_DMA_SEGNUM);

	sys = readl(cam->vi + CV181X_DMA_SYS_CONTROL);
	sys |= CV181X_DMA_SYS_BASE_SEL | CV181X_DMA_SYS_STRIDE_SEL |
	       CV181X_DMA_SYS_SEGLEN_SEL | CV181X_DMA_SYS_SEGNUM_SEL;
	writel(sys, cam->vi + CV181X_DMA_SYS_CONTROL);
	cv181x_vi_dma_set_addr(cam, addr);
}
EXPORT_SYMBOL_GPL(cv181x_vi_dma_configure);

void cv181x_vi_start(struct cv181x_camera_dev *cam)
{
	u32 size;

	size = FIELD_PREP(GENMASK(12, 0), cam->pix.width - 1) |
	       FIELD_PREP(GENMASK(28, 16), cam->pix.height - 1);
	writel(size, cam->vi + CV181X_CSIBDG_CH0_SIZE);
	writel(0, cam->vi + CV181X_CSIBDG_DPCM_MODE);
	writel(0, cam->vi + CV181X_CSIBDG_LD_DPCM_MODE);
	writel(U32_MAX, cam->vi + CV181X_CSIBDG_INT_STATUS0);
	writel(U32_MAX, cam->vi + CV181X_CSIBDG_INT_STATUS1);

	writel(CV181X_CSIBDG_INT_CH0_VS |
	       CV181X_CSIBDG_INT_CH0_SIZE_ERROR |
	       CV181X_CSIBDG_INT_DMA_ERROR |
	       CV181X_CSIBDG_INT_FIFO_OVERFLOW,
	       cam->vi + CV181X_CSIBDG_INT_CTRL);
}
EXPORT_SYMBOL_GPL(cv181x_vi_start);

void cv181x_vi_enable(struct cv181x_camera_dev *cam)
{
	u32 top = FIELD_PREP(CV181X_CSIBDG_TOP_CSI_MODE_MASK, 1) |
		  CV181X_CSIBDG_TOP_CH0_DMA_WR_ENABLE |
		  CV181X_CSIBDG_TOP_CSI_ENABLE |
		  CV181X_CSIBDG_TOP_SHDW_READ_SEL;

	if (cam->fmt->csi_dt == MIPI_CSI2_DT_YUV422_8B)
		top |= CV181X_CSIBDG_TOP_CSI_IN_FORMAT |
		       CV181X_CSIBDG_TOP_YUV_PACK_MODE;

	writel(top | CV181X_CSIBDG_TOP_CSI_UP_REG,
	       cam->vi + CV181X_CSIBDG_TOP_CTRL);
}
EXPORT_SYMBOL_GPL(cv181x_vi_enable);

void cv181x_vi_stop(struct cv181x_camera_dev *cam)
{
	u32 top;

	writel(0, cam->vi + CV181X_CSIBDG_INT_CTRL);
	writel(U32_MAX, cam->vi + CV181X_CSIBDG_INT_STATUS0);
	writel(U32_MAX, cam->vi + CV181X_CSIBDG_INT_STATUS1);

	top = readl(cam->vi + CV181X_CSIBDG_TOP_CTRL);
	top &= ~(CV181X_CSIBDG_TOP_CSI_ENABLE |
		 CV181X_CSIBDG_TOP_CH0_DMA_WR_ENABLE);
	writel(top | CV181X_CSIBDG_TOP_CSI_UP_REG,
	       cam->vi + CV181X_CSIBDG_TOP_CTRL);
}
EXPORT_SYMBOL_GPL(cv181x_vi_stop);

bool cv181x_vi_irq_status(struct cv181x_camera_dev *cam)
{
	u32 status0 = readl(cam->vi + CV181X_CSIBDG_INT_STATUS0);
	u32 status1 = readl(cam->vi + CV181X_CSIBDG_INT_STATUS1);

	writel(status0, cam->vi + CV181X_CSIBDG_INT_STATUS0);
	writel(status1, cam->vi + CV181X_CSIBDG_INT_STATUS1);
	if (status0 & CV181X_CSIBDG_STATUS0_CH0_ERRORS ||
	    status1 & CV181X_CSIBDG_STATUS1_ERRORS)
		dev_dbg(cam->dev,
			"CSI bridge error: status0=%#x status1=%#x\n",
			status0, status1);

	return status0 || status1;
}
EXPORT_SYMBOL_GPL(cv181x_vi_irq_status);

MODULE_DESCRIPTION("Sophgo CV181x video-input bridge support");
MODULE_LICENSE("GPL");
