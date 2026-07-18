// SPDX-License-Identifier: GPL-2.0-only
/* Shared Sophgo CV181x camera format support */

#include <linux/module.h>

#include <media/mipi-csi2.h>

#include "cv181x-camera.h"

const struct cv181x_camera_format cv181x_camera_formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_UYVY,
		.mbus_code = MEDIA_BUS_FMT_UYVY8_1X16,
		.csi_dt = MIPI_CSI2_DT_YUV422_8B,
	}, {
		.fourcc = V4L2_PIX_FMT_YUYV,
		.mbus_code = MEDIA_BUS_FMT_YUYV8_1X16,
		.csi_dt = MIPI_CSI2_DT_YUV422_8B,
	}, {
		/* The CSI bridge expands Bayer input to packed 12-bit samples. */
		.fourcc = V4L2_PIX_FMT_SBGGR12P,
		.mbus_code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.csi_dt = MIPI_CSI2_DT_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG12P,
		.mbus_code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.csi_dt = MIPI_CSI2_DT_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG12P,
		.mbus_code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.csi_dt = MIPI_CSI2_DT_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB12P,
		.mbus_code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.csi_dt = MIPI_CSI2_DT_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR12P,
		.mbus_code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.csi_dt = MIPI_CSI2_DT_RAW10,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG12P,
		.mbus_code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.csi_dt = MIPI_CSI2_DT_RAW10,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG12P,
		.mbus_code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.csi_dt = MIPI_CSI2_DT_RAW10,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB12P,
		.mbus_code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.csi_dt = MIPI_CSI2_DT_RAW10,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR12P,
		.mbus_code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.csi_dt = MIPI_CSI2_DT_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG12P,
		.mbus_code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.csi_dt = MIPI_CSI2_DT_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG12P,
		.mbus_code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.csi_dt = MIPI_CSI2_DT_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB12P,
		.mbus_code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.csi_dt = MIPI_CSI2_DT_RAW12,
	},
};
EXPORT_SYMBOL_GPL(cv181x_camera_formats);

static const unsigned int cv181x_vi_format_indices[] = {
	0, 1, 10, 11, 12, 13,
};

const unsigned int cv181x_num_camera_formats =
	ARRAY_SIZE(cv181x_camera_formats);
EXPORT_SYMBOL_GPL(cv181x_num_camera_formats);

const unsigned int cv181x_num_vi_formats = ARRAY_SIZE(cv181x_vi_format_indices);
EXPORT_SYMBOL_GPL(cv181x_num_vi_formats);

const struct cv181x_camera_format *cv181x_camera_find_fourcc(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < cv181x_num_vi_formats; i++) {
		const struct cv181x_camera_format *fmt;

		fmt = &cv181x_camera_formats[cv181x_vi_format_indices[i]];
		if (fmt->fourcc == fourcc)
			return fmt;
	}

	return &cv181x_camera_formats[0];
}
EXPORT_SYMBOL_GPL(cv181x_camera_find_fourcc);

const struct cv181x_camera_format *cv181x_camera_vi_format(unsigned int index)
{
	if (index >= cv181x_num_vi_formats)
		return NULL;

	return &cv181x_camera_formats[cv181x_vi_format_indices[index]];
}
EXPORT_SYMBOL_GPL(cv181x_camera_vi_format);

const struct cv181x_camera_format *cv181x_camera_find_mbus(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cv181x_camera_formats); i++) {
		if (cv181x_camera_formats[i].mbus_code == code)
			return &cv181x_camera_formats[i];
	}

	return &cv181x_camera_formats[0];
}
EXPORT_SYMBOL_GPL(cv181x_camera_find_mbus);

void cv181x_camera_fill_pix(struct v4l2_pix_format *pix,
			    const struct cv181x_camera_format *fmt,
			    u32 width, u32 height)
{
	pix->width = clamp(width, 1U, CV181X_CSI2_MAX_WIDTH);
	pix->height = clamp(height, 1U, CV181X_CSI2_MAX_HEIGHT);
	pix->pixelformat = fmt->fourcc;
	pix->field = V4L2_FIELD_NONE;
	if (fmt->csi_dt == MIPI_CSI2_DT_YUV422_8B)
		pix->bytesperline = pix->width * 2;
	else
		pix->bytesperline = ALIGN(3 * DIV_ROUND_UP(pix->width, 2),
					  16);
	pix->sizeimage = pix->bytesperline * pix->height;
	pix->colorspace = fmt->csi_dt == MIPI_CSI2_DT_YUV422_8B ?
			  V4L2_COLORSPACE_SRGB : V4L2_COLORSPACE_RAW;
	pix->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	pix->quantization = V4L2_QUANTIZATION_DEFAULT;
	pix->xfer_func = V4L2_XFER_FUNC_DEFAULT;
}
EXPORT_SYMBOL_GPL(cv181x_camera_fill_pix);

MODULE_DESCRIPTION("Sophgo CV181x camera shared format support");
MODULE_LICENSE("GPL");
