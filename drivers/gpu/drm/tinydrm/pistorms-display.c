/*
 * DRM driver for mindsensors.com PiStorms display
 *
 * Copyright 2018 David Lechner <david@lechnology.com>
 *
 * Based on:
 * DRM driver for Multi-Inno pistorms panels
 *
 * Copyright 2016 Noralf Trønnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <drm/tinydrm/ili9341.h>
#include <drm/tinydrm/mipi-dbi.h>
#include <drm/tinydrm/tinydrm-helpers.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <video/mipi_display.h>

static int pistorms_init(struct mipi_dbi *mipi)
{
	struct tinydrm_device *tdev = &mipi->tinydrm;
	struct device *dev = tdev->drm->dev;
	u8 addr_mode;
	int ret;

	DRM_DEBUG_KMS("\n");

	ret = regulator_enable(mipi->regulator);
	if (ret) {
		dev_err(dev, "Failed to enable regulator %d\n", ret);
		return ret;
	}

	/* Avoid flicker by skipping setup if the bootloader has done it */
	if (mipi_dbi_display_is_on(mipi))
		return 0;

	mipi_dbi_hw_reset(mipi);
	ret = mipi_dbi_command(mipi, MIPI_DCS_SOFT_RESET);
	if (ret) {
		dev_err(dev, "Error sending command %d\n", ret);
		regulator_disable(mipi->regulator);
		return ret;
	}

	msleep(20);

	mipi_dbi_command(mipi, MIPI_DCS_SET_DISPLAY_OFF);

	mipi_dbi_command(mipi, ILI9341_PWCTRLB, 0x00, 0xc1, 0x30);
	mipi_dbi_command(mipi, ILI9341_PWRSEQ, 0x64, 0x03, 0x12, 0x81);
	mipi_dbi_command(mipi, ILI9341_DTCTRLA, 0x85, 0x00, 0x78);
	mipi_dbi_command(mipi, ILI9341_PWCTRLA, 0x39, 0x2c, 0x00, 0x34, 0x02);
	mipi_dbi_command(mipi, ILI9341_PUMPCTRL, 0x20);
	mipi_dbi_command(mipi, ILI9341_DTCTRLB, 0x00, 0x00);

	/* Power Control */
	mipi_dbi_command(mipi, ILI9341_PWCTRL1, 0x23);
	mipi_dbi_command(mipi, ILI9341_PWCTRL2, 0x10);
	/* VCOM */
	mipi_dbi_command(mipi, ILI9341_VMCTRL1, 0x3e, 0x28);
	mipi_dbi_command(mipi, ILI9341_VMCTRL2, 0x86);

	/* Memory Access Control */
	mipi_dbi_command(mipi, MIPI_DCS_SET_PIXEL_FORMAT, 0x55);

	switch (mipi->rotation) {
	default:
		addr_mode = ILI9341_MADCTL_MV | ILI9341_MADCTL_MY |
			    ILI9341_MADCTL_MX;
		break;
	case 90:
		addr_mode = ILI9341_MADCTL_MY;
		break;
	case 180:
		addr_mode = ILI9341_MADCTL_MV;
		break;
	case 270:
		addr_mode = ILI9341_MADCTL_MX;
		break;
	}
	addr_mode |= ILI9341_MADCTL_BGR;
	mipi_dbi_command(mipi, MIPI_DCS_SET_ADDRESS_MODE, addr_mode);

	/* Frame Rate */
	mipi_dbi_command(mipi, ILI9341_FRMCTR1, 0x00, 0x1b);

	/* Gamma */
	mipi_dbi_command(mipi, ILI9341_EN3GAM, 0x00);
	mipi_dbi_command(mipi, MIPI_DCS_SET_GAMMA_CURVE, 0x01);
	mipi_dbi_command(mipi, ILI9341_PGAMCTRL,
			 0x0f, 0x31, 0x2b, 0x0c, 0x0e, 0x08, 0x4e, 0xf1,
			 0x37, 0x07, 0x10, 0x03, 0x0e, 0x09, 0x00);
	mipi_dbi_command(mipi, ILI9341_NGAMCTRL,
			 0x00, 0x0e, 0x14, 0x03, 0x11, 0x07, 0x31, 0xc1,
			 0x48, 0x08, 0x0f, 0x0c, 0x31, 0x36, 0x0f);

	/* DDRAM */
	mipi_dbi_command(mipi, ILI9341_ETMOD, 0x07);

	/* Display */
	mipi_dbi_command(mipi, ILI9341_DISCTRL, 0x08, 0x82, 0x27, 0x00);
	mipi_dbi_command(mipi, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(100);

	mipi_dbi_command(mipi, MIPI_DCS_SET_DISPLAY_ON);
	msleep(100);

	return 0;
}

static void pistorms_fini(void *data)
{
	struct mipi_dbi *mipi = data;

	DRM_DEBUG_KMS("\n");
	regulator_disable(mipi->regulator);
}

static const struct drm_simple_display_pipe_funcs pistorms_pipe_funcs = {
	.enable = mipi_dbi_pipe_enable,
	.disable = mipi_dbi_pipe_disable,
	.update = tinydrm_display_pipe_update,
	.prepare_fb = tinydrm_display_pipe_prepare_fb,
};

static const struct drm_display_mode pistorms_mode = {
	TINYDRM_MODE(240, 320, 37, 49),
};

DEFINE_DRM_GEM_CMA_FOPS(pistorms_fops);

static struct drm_driver pistorms_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME |
				  DRIVER_ATOMIC,
	.fops			= &pistorms_fops,
	TINYDRM_GEM_DRIVER_OPS,
	.lastclose		= tinydrm_lastclose,
	.debugfs_init		= mipi_dbi_debugfs_init,
	.name			= "pistorms",
	.desc			= "mindsensors.com PiStorms",
	.date			= "20180412",
	.major			= 1,
	.minor			= 0,
};

static const struct of_device_id pistorms_of_match[] = {
	{ .compatible = "ev3dev,pistorms-display" },
	{},
};
MODULE_DEVICE_TABLE(of, pistorms_of_match);

static const struct spi_device_id pistorms_id[] = {
	{ "pistorms-display", 0 },
	{ },
};
MODULE_DEVICE_TABLE(spi, pistorms_id);

static int pistorms_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct tinydrm_device *tdev;
	struct mipi_dbi *mipi;
	struct gpio_desc *dc;
	u32 rotation = 0;
	int ret;

	mipi = devm_kzalloc(dev, sizeof(*mipi), GFP_KERNEL);
	if (!mipi)
		return -ENOMEM;

	mipi->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(mipi->reset)) {
		dev_err(dev, "Failed to get gpio 'reset'\n");
		return PTR_ERR(mipi->reset);
	}

	dc = devm_gpiod_get_optional(dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(dc)) {
		dev_err(dev, "Failed to get gpio 'dc'\n");
		return PTR_ERR(dc);
	}

	mipi->regulator = devm_regulator_get(dev, "power");
	if (IS_ERR(mipi->regulator))
		return PTR_ERR(mipi->regulator);

	mipi->backlight = tinydrm_of_find_backlight(dev);
	if (IS_ERR(mipi->backlight))
		return PTR_ERR(mipi->backlight);

	device_property_read_u32(dev, "rotation", &rotation);

	ret = mipi_dbi_spi_init(spi, mipi, dc);
	if (ret)
		return ret;

	ret = mipi_dbi_init(&spi->dev, mipi, &pistorms_pipe_funcs,
			    &pistorms_driver, &pistorms_mode, rotation);
	if (ret)
		return ret;

	ret = pistorms_init(mipi);
	if (ret)
		return ret;

	/* use devres to fini after drm unregister (drv->remove is before) */
	ret = devm_add_action(dev, pistorms_fini, mipi);
	if (ret) {
		pistorms_fini(mipi);
		return ret;
	}

	tdev = &mipi->tinydrm;

	ret = devm_tinydrm_register(tdev);
	if (ret)
		return ret;

	spi_set_drvdata(spi, mipi);

	DRM_DEBUG_DRIVER("Initialized %s:%s @%uMHz on minor %d\n",
			 tdev->drm->driver->name, dev_name(dev),
			 spi->max_speed_hz / 1000000,
			 tdev->drm->primary->index);

	return 0;
}

static void pistorms_shutdown(struct spi_device *spi)
{
	struct mipi_dbi *mipi = spi_get_drvdata(spi);

	tinydrm_shutdown(&mipi->tinydrm);
}

static int __maybe_unused pistorms_pm_suspend(struct device *dev)
{
	struct mipi_dbi *mipi = dev_get_drvdata(dev);
	int ret;

	ret = tinydrm_suspend(&mipi->tinydrm);
	if (ret)
		return ret;

	pistorms_fini(mipi);

	return 0;
}

static int __maybe_unused pistorms_pm_resume(struct device *dev)
{
	struct mipi_dbi *mipi = dev_get_drvdata(dev);
	int ret;

	ret = pistorms_init(mipi);
	if (ret)
		return ret;

	return tinydrm_resume(&mipi->tinydrm);
}

static const struct dev_pm_ops pistorms_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pistorms_pm_suspend, pistorms_pm_resume)
};

static struct spi_driver pistorms_spi_driver = {
	.driver = {
		.name = "pistorms-display",
		.owner = THIS_MODULE,
		.of_match_table = pistorms_of_match,
		.pm = &pistorms_pm_ops,
	},
	.id_table = pistorms_id,
	.probe = pistorms_probe,
	.shutdown = pistorms_shutdown,
};
module_spi_driver(pistorms_spi_driver);

MODULE_DESCRIPTION("mindsensors.com PiStorms DRM driver");
MODULE_AUTHOR("David Lechner");
MODULE_LICENSE("GPL");
