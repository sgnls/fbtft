/*
 *
 * Copyright (C) 2013, Noralf Tronnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spi/spi.h>

#include "fbtft.h"

#define DRVNAME "fbtft_device"

#define MAX_GPIOS 32

struct spi_device *spi_device;
struct platform_device *p_device;

static char *name;
module_param(name, charp, 0);
MODULE_PARM_DESC(name, "Devicename (required). " \
"name=list => list all supported devices.");

static unsigned rotate;
module_param(rotate, uint, 0);
MODULE_PARM_DESC(rotate, "Rotate display 0=normal, 1=clockwise, " \
"2=upside down, 3=counterclockwise (not supported by all drivers)");

static unsigned busnum;
module_param(busnum, uint, 0);
MODULE_PARM_DESC(busnum, "SPI bus number (default=0)");

static unsigned cs;
module_param(cs, uint, 0);
MODULE_PARM_DESC(cs, "SPI chip select (default=0)");

static unsigned speed;
module_param(speed, uint, 0);
MODULE_PARM_DESC(speed, "SPI speed (override device default)");

static int mode = -1;
module_param(mode, int, 0);
MODULE_PARM_DESC(mode, "SPI mode (override device default)");

static char *gpios[MAX_GPIOS] = { NULL, };
static int gpios_num;
module_param_array(gpios, charp, &gpios_num, 0);
MODULE_PARM_DESC(gpios,
"List of gpios. Comma separated with the form: reset:23,dc:24 " \
"(when overriding the default, all gpios must be specified)");

static unsigned fps;
module_param(fps, uint, 0);
MODULE_PARM_DESC(fps, "Frames per second (override driver default)");

static char *gamma;
module_param(gamma, charp, 0);
MODULE_PARM_DESC(gamma,
"String representation of Gamma Curve(s). Driver specific.");

static int txbuflen;
module_param(txbuflen, int, 0);
MODULE_PARM_DESC(txbuflen, "txbuflen (override driver default)");

static int bgr = -1;
module_param(bgr, int, 0);
MODULE_PARM_DESC(bgr,
"BGR bit (supported by some drivers).");

static unsigned startbyte;
module_param(startbyte, uint, 0);
MODULE_PARM_DESC(startbyte, "Sets the Start byte used by some SPI displays.");

static bool custom;
module_param(custom, bool, 0);
MODULE_PARM_DESC(custom, "Add a custom display device. " \
"Use speed= argument to make it a SPI device, else platform_device");

static unsigned width;
module_param(width, uint, 0);
MODULE_PARM_DESC(width, "Display width, used with the custom argument");

static unsigned height;
module_param(height, uint, 0);
MODULE_PARM_DESC(height, "Display height, used with the custom argument");

static unsigned buswidth;
module_param(buswidth, uint, 0);
MODULE_PARM_DESC(buswidth, "Display bus width, used with the custom argument");

static int init[FBTFT_MAX_INIT_SEQUENCE];
static int init_num = 0;
module_param_array(init, int, &init_num, 0);
MODULE_PARM_DESC(init, "Init sequence, used with the custom argument");

static unsigned long debug = 0;
module_param(debug, ulong , 0);
MODULE_PARM_DESC(debug,"level: 0-7 (the remaining 29 bits is for advanced usage)");

static unsigned verbose = 3;
module_param(verbose, uint, 0);
MODULE_PARM_DESC(verbose,
"0 silent, >0 show gpios, >1 show devices, >2 show devices before (default=3)");


struct fbtft_device_display {
	char *name;
	struct spi_board_info *spi;
	struct platform_device *pdev;
};

static void fbtft_device_pdev_release(struct device *dev);

/* Supported displays in alphabetical order */
static struct fbtft_device_display displays[] = {
	{
		.name = "adafruit18fb",
		.spi = &(struct spi_board_info) {
			.modalias = "adafruit18fb",
			.max_speed_hz = 4000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{ "led", 23 },
					{},
				},
			}
		}
	}, {
		.name = "adafruit18greenfb",
		.spi = &(struct spi_board_info) {
			.modalias = "adafruit18greenfb",
			.max_speed_hz = 4000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{ "led", 23 },
					{},
				},
			}
		}
	}, {
		.name = "adafruit22",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_hx8340bn",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 9,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "led", 23 },
					{},
				},
			}
		}
	}, {
		.name = "adafruit22fb",
		.spi = &(struct spi_board_info) {
			.modalias = "adafruit22fb",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "led", 23 },
					{},
				},
			}
		}
	}, {
		.name = "flexfb",
		.spi = &(struct spi_board_info) {
			.modalias = "flexfb",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{},
				},
			}
		}
	}, {
		.name = "flexpfb",
		.pdev = &(struct platform_device) {
			.name = "flexpfb",
			.id = 0,
			.dev = {
			.release = fbtft_device_pdev_release,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 17 },
					{ "dc", 1 },
					{ "wr", 0 },
					{ "cs", 21 },
					{ "db00", 9 },
					{ "db01", 11 },
					{ "db02", 18 },
					{ "db03", 23 },
					{ "db04", 24 },
					{ "db05", 25 },
					{ "db06", 8 },
					{ "db07", 7 },
					{ "led", 4 },
					{},
				},
			},
			}
		}
	}, {
		.name = "hy28afb",
		.spi = &(struct spi_board_info) {
			.modalias = "hy28afb",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_3,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "led", 18 },
					{},
				},
			}
		}
	}, {
		.name = "ili9341fb",
		.spi = &(struct spi_board_info) {
			.modalias = "ili9341fb",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 23 },
					{ "led", 24 },
					{},
				},
			}
		}
	}, {
		.name = "itdb28",
		.pdev = &(struct platform_device) {
			.name = "fb_ili9325",
			.id = 0,
			.dev = {
			.release = fbtft_device_pdev_release,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{},
				},
			},
			}
		}
	}, {
		.name = "itdb28_spi",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ili9325",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{},
				},
			}
		}
	}, {
		.name = "itdb28fb",
		.pdev = &(struct platform_device) {
			.name = "itdb28fb",
			.id = 0,
			.dev = {
			.release = fbtft_device_pdev_release,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 17 },
					{ "dc", 1 },
					{ "wr", 0 },
					{ "cs", 21 },
					{ "db00", 9 },
					{ "db01", 11 },
					{ "db02", 18 },
					{ "db03", 23 },
					{ "db04", 24 },
					{ "db05", 25 },
					{ "db06", 8 },
					{ "db07", 7 },
					{ "led", 4 },
					{},
				},
			},
			}
		}
	}, {
		.name = "itdb28spifb",
		.spi = &(struct spi_board_info) {
			.modalias = "itdb28spifb",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{},
				},
			}
		}
	}, {
		.name = "mi0283qt-9a",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ili9341",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 9,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "led", 18 },
					{},
				},
			}
		}
	}, {
		.name = "nokia3310",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_pcd8544",
			.max_speed_hz = 400000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
				},
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{ "led", 23 },
					{},
				},
			}
		}
	}, {
		.name = "nokia3310fb",
		.spi = &(struct spi_board_info) {
			.modalias = "nokia3310fb",
			.max_speed_hz = 4000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{ "led", 23 },
					{},
				},
			}
		}
	}, {
		.name = "r61505ufb",
		.spi = &(struct spi_board_info) {
			.modalias = "r61505ufb",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 23 },
					{ "led", 24 },
					{ "dc", 7 },
					{},
				},
			}
		}
	}, {
		.name = "sainsmart18",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_st7735r",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
				},
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{},
				},
			}
		}
	}, {
		.name = "sainsmart18fb",
		.spi = &(struct spi_board_info) {
			.modalias = "sainsmart18fb",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{},
				},
			}
		}
	}, {
		.name = "sainsmart32spifb",
		.spi = &(struct spi_board_info) {
			.modalias = "sainsmart32spifb",
			.max_speed_hz = 16000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{},
				},
			}
		}
	}, {
		.name = "sainsmart32fb",
		.pdev = &(struct platform_device) {
			.name = "sainsmart32fb",
			.id = 0,
			.dev = {
			.release = fbtft_device_pdev_release,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{},
				},
			},
		},
		}
	}, {
		.name = "spidev",
		.spi = &(struct spi_board_info) {
			.modalias = "spidev",
			.max_speed_hz = 500000,
			.bus_num = 0,
			.chip_select = 0,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{},
				},
			}
		}
	}, {
		.name = "ssd1351fb",
		.spi = &(struct spi_board_info) {
			.modalias = "ssd1351fb",
			.max_speed_hz = 20000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 24 },
					{ "dc", 25 },
					{},
				},
			}
		}
	}, {
		/* This should be the last item.
		   Used with the custom argument */
		.name = "",
		.spi = &(struct spi_board_info) {
			.modalias = "",
			.max_speed_hz = 0,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{},
				},
			}
		},
		.pdev = &(struct platform_device) {
			.name = "",
			.id = 0,
			.dev = {
			.release = fbtft_device_pdev_release,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{},
				},
			},
		},
		},
	}
};

/* used if gpios parameter is present */
static struct fbtft_gpio fbtft_device_param_gpios[MAX_GPIOS+1] = { };

static void fbtft_device_pdev_release(struct device *dev)
{
/* Needed to silence this message:
Device 'xxx' does not have a release() function, it is broken and must be fixed
*/
}

static int spi_device_found(struct device *dev, void *data)
{
	struct spi_device *spi = container_of(dev, struct spi_device, dev);

	pr_info(DRVNAME":      %s %s %dkHz %d bits mode=0x%02X\n",
		spi->modalias, dev_name(dev), spi->max_speed_hz/1000,
		spi->bits_per_word, spi->mode);

	return 0;
}

static void pr_spi_devices(void)
{
	pr_info(DRVNAME":  SPI devices registered:\n");
	bus_for_each_dev(&spi_bus_type, NULL, NULL, spi_device_found);
}

static int p_device_found(struct device *dev, void *data)
{
	struct platform_device
	*pdev = container_of(dev, struct platform_device, dev);

	if (strstr(pdev->name, "fb"))
		pr_info(DRVNAME":      %s id=%d pdata? %s\n",
				pdev->name, pdev->id,
				pdev->dev.platform_data ? "yes" : "no");

	return 0;
}

static void pr_p_devices(void)
{
	pr_info(DRVNAME":  'fb' Platform devices registered:\n");
	bus_for_each_dev(&platform_bus_type, NULL, NULL, p_device_found);
}

static void fbtft_device_delete(struct spi_master *master, unsigned cs)
{
	struct device *dev;
	char str[32];

	snprintf(str, sizeof(str), "%s.%u", dev_name(&master->dev), cs);

	dev = bus_find_device_by_name(&spi_bus_type, NULL, str);
	if (dev) {
		pr_err(DRVNAME": Deleting %s\n", str);
		device_del(dev);
	}
}

static int __init fbtft_device_init(void)
{
	struct spi_master *master = NULL;
	struct spi_board_info *spi = NULL;
	struct fbtft_platform_data *pdata;
	const struct fbtft_gpio *gpio = NULL;
	char *p_name, *p_num;
	bool found = false;
	int i;
	long val;
	int ret = 0;

	pr_debug("\n\n"DRVNAME": init\n");

	if (init_num > FBTFT_MAX_INIT_SEQUENCE) {
		pr_err(DRVNAME \
			":  init parameter: exceeded max array size: %d\n",
			FBTFT_MAX_INIT_SEQUENCE);
		return -EINVAL;
	}

	/* parse module parameter: gpios */
	if (gpios_num > MAX_GPIOS) {
		pr_err(DRVNAME \
			":  gpios parameter: exceeded max array size: %d\n",
			MAX_GPIOS);
		return -EINVAL;
	}
	if (gpios_num > 0) {
		for (i = 0; i < gpios_num; i++) {
			if (strchr(gpios[i], ':') == NULL) {
				pr_err(DRVNAME \
					":  error: missing ':' in gpios parameter: %s\n",
					gpios[i]);
				return -EINVAL;
			}
			p_num = gpios[i];
			p_name = strsep(&p_num, ":");
			if (p_name == NULL || p_num == NULL) {
				pr_err(DRVNAME \
					":  something bad happened parsing gpios parameter: %s\n",
					gpios[i]);
				return -EINVAL;
			}
			ret = kstrtol(p_num, 10, &val);
			if (ret) {
				pr_err(DRVNAME \
					":  could not parse number in gpios parameter: %s:%s\n",
					p_name, p_num);
				return -EINVAL;
			}
			strcpy(fbtft_device_param_gpios[i].name, p_name);
			fbtft_device_param_gpios[i].gpio = (int) val;
		}
		gpio = fbtft_device_param_gpios;
	}

	if (verbose > 2)
		pr_spi_devices(); /* print list of registered SPI devices */

	if (verbose > 2)
		pr_p_devices(); /* print list of 'fb' platform devices */

	if (name == NULL) {
		pr_err(DRVNAME":  missing module parameter: 'name'\n");
		return -EINVAL;
	}

	pr_debug(DRVNAME":  name='%s', busnum=%d, cs=%d\n", name, busnum, cs);

	if (rotate > 3) {
		pr_warn("argument 'rotate' illegal value: %d (0-3). Setting it to 0.\n",
			rotate);
		rotate = 0;
	}

	/* name=list lists all supported displays */
	if (strncmp(name, "list", 32) == 0) {
		pr_info(DRVNAME":  Supported displays:\n");

		for (i = 0; i < ARRAY_SIZE(displays); i++)
			pr_info(DRVNAME":      %s\n", displays[i].name);
		return -ECANCELED;
	}

	if (custom) {
		i = ARRAY_SIZE(displays) - 1;
		displays[i].name = name;
		if (speed == 0) {
			displays[i].pdev->name = name;
			displays[i].spi = NULL;
		} else {
			strncpy(displays[i].spi->modalias, name, SPI_NAME_SIZE);
			displays[i].pdev = NULL;
		}
	}

	for (i = 0; i < ARRAY_SIZE(displays); i++) {
		if (strncmp(name, displays[i].name, 32) == 0) {
			if (displays[i].spi) {
				master = spi_busnum_to_master(busnum);
				if (!master) {
					pr_err(DRVNAME \
						":  spi_busnum_to_master(%d) returned NULL\n",
						busnum);
					return -EINVAL;
				}
				/* make sure bus:cs is available */
				fbtft_device_delete(master, cs);
				spi = displays[i].spi;
				spi->chip_select = cs;
				spi->bus_num = busnum;
				if (speed)
					spi->max_speed_hz = speed;
				if (mode != -1)
					spi->mode = mode;
				pdata = (void *)spi->platform_data;
			} else if (displays[i].pdev) {
				p_device = displays[i].pdev;
				pdata = p_device->dev.platform_data;
			} else {
				pr_err(DRVNAME": broken displays array\n");
				return -EINVAL;
			}

			pdata->rotate = rotate;
			if (bgr == 0)
				pdata->bgr = false;
			else if (bgr == 1)
				pdata->bgr = true;
			if (startbyte)
				pdata->startbyte = startbyte;
			if (gamma)
				pdata->gamma = gamma;
			pdata->display.debug = debug;
			if (fps)
				pdata->fps = fps;
			if (txbuflen)
				pdata->txbuflen = txbuflen;
			if (gpio)
				pdata->gpios = gpio;
			if (custom) {
				pdata->display.width = width;
				pdata->display.height = height;
				pdata->display.buswidth = buswidth;
				if (init_num)
					pdata->display.init_sequence = init;
			}

			if (displays[i].spi) {
				spi_device = spi_new_device(master, spi);
				put_device(&master->dev);
				if (!spi_device) {
					pr_err(DRVNAME \
						":    spi_new_device() returned NULL\n");
					return -EPERM;
				}
				found = true;
				break;
			} else {
				ret = platform_device_register(p_device);
				if (ret < 0) {
					pr_err(DRVNAME \
						":    platform_device_register() returned %d\n",
						ret);
					return ret;
				}
				found = true;
				break;
			}
		}
	}

	if (!found) {
		pr_err(DRVNAME":  display not supported: '%s'\n", name);
		return -EINVAL;
	}

	if (verbose && pdata && pdata->gpios) {
		gpio = pdata->gpios;
		pr_info(DRVNAME":  GPIOS used by '%s':\n", name);
		found = false;
		while (verbose && gpio->name[0]) {
			pr_info(DRVNAME":    '%s' = GPIO%d\n",
				gpio->name, gpio->gpio);
			gpio++;
			found = true;
		}
		if (!found)
			pr_info(DRVNAME":    (none)\n");
	}

	if (spi_device && (verbose > 1))
		pr_spi_devices();
	if (p_device && (verbose > 1))
		pr_p_devices();

	return 0;
}

static void __exit fbtft_device_exit(void)
{
	pr_debug(DRVNAME" - exit\n");

	if (spi_device) {
		device_del(&spi_device->dev);
		kfree(spi_device);
	}

	if (p_device)
		platform_device_unregister(p_device);

}

module_init(fbtft_device_init);
module_exit(fbtft_device_exit);

MODULE_DESCRIPTION("Add a FBTFT device.");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
