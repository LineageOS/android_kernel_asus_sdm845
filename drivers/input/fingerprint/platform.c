/*
 * platform indepent driver interface
 *
 * Coypritht (c) 2017 Goodix
 */
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/timer.h>
#include <linux/err.h>

#include "gf_spi.h"

#if defined(USE_SPI_BUS)
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#elif defined(USE_PLATFORM_BUS)
#include <linux/platform_device.h>
#endif

int gf_parse_dts(struct gf_dev *gf_dev)
{
	int rc = 0;
	struct device *dev = &gf_dev->spi->dev;
	struct device_node *np = dev->of_node;

	gf_dev->reset_gpio = of_get_named_gpio(np, "goodix,gpio_reset", 0);
	if (gf_dev->reset_gpio < 0) {
		pr_err("falied to get reset gpio!\n");
		return gf_dev->reset_gpio;
	}

	rc = devm_gpio_request(dev, gf_dev->reset_gpio, "goodix_reset");
	if (rc) {
		pr_err("failed to request reset gpio, rc = %d\n", rc);
		goto err_reset;
	}
	gpio_direction_output(gf_dev->reset_gpio,0);
	pr_info("gf_dev->reset_gpio = %d , value = %d\n", gf_dev->reset_gpio, gpio_get_value(gf_dev->reset_gpio));
	gf_dev->irq_gpio = of_get_named_gpio(np, "goodix,gpio_irq", 0);
	if (gf_dev->irq_gpio < 0) {
		pr_err("falied to get irq gpio!\n");
		return gf_dev->irq_gpio;
	}
    pr_info("gf_dev->irq_gpio = %d\n", gf_dev->irq_gpio);
	rc = devm_gpio_request(dev, gf_dev->irq_gpio, "goodix_irq");
	if (rc) {
		pr_err("failed to request irq gpio, rc = %d\n", rc);
		goto err_irq;
	}
	gpio_direction_input(gf_dev->irq_gpio);

err_irq:
	devm_gpio_free(dev, gf_dev->reset_gpio);
err_reset:
	return rc;
}

void gf_cleanup(struct gf_dev *gf_dev)
{
	pr_info("[info] %s\n", __func__);

	if (gpio_is_valid(gf_dev->irq_gpio)) {
		gpio_free(gf_dev->irq_gpio);
		pr_info("remove irq_gpio success\n");
	}
	if (gpio_is_valid(gf_dev->reset_gpio)) {
		gpio_free(gf_dev->reset_gpio);
		pr_info("remove reset_gpio success\n");
	}
}

int gf_power_on(struct gf_dev *gf_dev)
{
	struct pinctrl *pctrl;
    struct pinctrl_state *pins_enable;
	struct device *dev = &gf_dev->spi->dev;
    int rc = 0;
    pctrl = devm_pinctrl_get(dev);
	if (IS_ERR(pctrl)) {
		dev_err(dev, "failed to get pinctrl\n");
		return PTR_ERR(pctrl);
	};
	pins_enable = pinctrl_lookup_state(pctrl, "goodix_ldo_enable");
	if (IS_ERR(pins_enable)) {
		rc = PTR_ERR(pins_enable);
		dev_err(dev, "Could not get active pinstates, err:%d\n", rc);
		return PTR_ERR(pins_enable);
	};
    rc = pinctrl_select_state(pctrl, pins_enable);
    if (rc)
		pr_err("failed to set pins_enable, ret = %d\n", rc);
    mdelay(15);
    gpio_direction_output(gf_dev->reset_gpio, 1);

	return rc;
}

int gf_power_off(struct gf_dev *gf_dev)
{
	struct pinctrl *pctrl;
    struct pinctrl_state *pins_disable;
    struct device *dev = &gf_dev->spi->dev;
    int rc = 0;
    pctrl = devm_pinctrl_get(dev);
	if (IS_ERR(pctrl)) {
		dev_err(dev, "failed to get pinctrl\n");
		return PTR_ERR(pctrl);
	};
	pins_disable = pinctrl_lookup_state(pctrl, "goodix_ldo_disable");
	if (IS_ERR(pins_disable)) {
		rc = PTR_ERR(pins_disable);
		dev_err(dev, "Could not get inactive pinstates, err:%d\n", rc);
		return PTR_ERR(pins_disable);
	};
    rc = pinctrl_select_state(pctrl, pins_disable);
    if (rc)
		pr_err("failed to set pins_disable, ret = %d\n", rc);

	return rc;
}

int gf_hw_reset(struct gf_dev *gf_dev, unsigned int delay_ms)
{
	if (gf_dev == NULL) {
		pr_info("Input buff is NULL.\n");
		return -1;
	}
	gpio_direction_output(gf_dev->reset_gpio, 1);
	gpio_set_value(gf_dev->reset_gpio, 0);
	mdelay(3);
	gpio_set_value(gf_dev->reset_gpio, 1);
	mdelay(delay_ms);
	return 0;
}

int gf_irq_num(struct gf_dev *gf_dev)
{
	if (gf_dev == NULL) {
		pr_info("Input buff is NULL.\n");
		return -1;
	} else {
		return gpio_to_irq(gf_dev->irq_gpio);
	}
}
