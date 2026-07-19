/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __CV181X_COMPONENTS_H__
#define __CV181X_COMPONENTS_H__

#include <linux/types.h>

struct cv181x_camera_dev;

void cv181x_csi2_init_lane_deskew(struct cv181x_camera_dev *cam);
int cv181x_csi2_start(struct cv181x_camera_dev *cam);
void cv181x_csi2_stop(struct cv181x_camera_dev *cam);

void cv181x_vi_dma_set_addr(struct cv181x_camera_dev *cam, dma_addr_t addr);
void cv181x_vi_dma_configure(struct cv181x_camera_dev *cam, dma_addr_t addr);
void cv181x_vi_start(struct cv181x_camera_dev *cam);
void cv181x_vi_enable(struct cv181x_camera_dev *cam);
void cv181x_vi_stop(struct cv181x_camera_dev *cam);
bool cv181x_vi_irq_status(struct cv181x_camera_dev *cam);

void cv181x_isp_start(struct cv181x_camera_dev *cam);
void cv181x_isp_reset(struct cv181x_camera_dev *cam);
void cv181x_isp_stop(struct cv181x_camera_dev *cam);
void cv181x_isp_trigger(struct cv181x_camera_dev *cam);
u32 cv181x_isp_irq_status(struct cv181x_camera_dev *cam);

#endif
