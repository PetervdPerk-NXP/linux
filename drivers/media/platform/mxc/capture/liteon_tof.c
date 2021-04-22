// SPDX-License-Identifier: GPL-2.0

/*
 * Liteon ToF camera driver
 *
 */

#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/v4l2-mediabus.h>
#include <media/v4l2-device.h>

struct liteon_tof {
	struct v4l2_subdev subdev;
	struct v4l2_captureparm cap_parm;
	struct media_pad mpad;
	struct device dev;
	struct clk *clk;
	int pwn_gpio;
	int rst_gpio;
};

struct liteon_tof_res {
	int width;
	int height;
};

struct liteon_tof_res liteon_tof_valid_res[] = {
    {224, 172},
    {224, 173},
    {224, 860},
    {224, 865},
    {224, 1548},
    {224, 1557},
};

struct liteon_tof_res liteon_tof_vga_valid_res[] = {
    {640, 240},
    {640, 241},
    {640, 1200},
    {640, 1205},
    {640, 2160},
    {640, 2169},
};

struct liteon_tof_res * liteon_valid_res = NULL;
int liteon_res_max = 0;

static int find_resulution(int width, int height)
{
	struct liteon_tof_res *ptr = liteon_valid_res;

	while (ptr != (liteon_valid_res + liteon_res_max)) {
		if ((ptr->width == width) && (ptr->height == height))
			return &liteon_valid_res[liteon_res_max - 1] - ptr;
		ptr++;
	}

	return -1;
}

static inline void liteon_tof_disable_power(struct liteon_tof *sensor)
{
	if (gpio_is_valid(sensor->pwn_gpio)) {
		gpio_direction_output(sensor->pwn_gpio, 0);
	}
}

static inline void liteon_tof_enable_power(struct liteon_tof *sensor)
{
	if (gpio_is_valid(sensor->pwn_gpio)) {
		gpio_direction_input(sensor->pwn_gpio);
	}
}

static int liteon_tof_s_power(struct v4l2_subdev *subdev, int on)
{
	return 0;
}

static int liteon_tof_g_parm(struct v4l2_subdev *subdev,
		struct v4l2_streamparm *sparm)
{
	struct liteon_tof *sensor = container_of(subdev, struct liteon_tof,
			subdev);
	struct device *dev = &sensor->dev;
	struct v4l2_captureparm *cparm = &sparm->parm.capture;
	int ret = 0;

	switch (sparm->type) {
	/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		memset(sparm, 0, sizeof(*sparm));
		cparm->capability = sensor->cap_parm.capability;
		cparm->capturemode = sensor->cap_parm.capturemode;
		ret = 0;
		break;
	default:
		dev_warn(dev, "Parameter type is unknown - %d\n", sparm->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int liteon_tof_s_parm(struct v4l2_subdev *subdev,
		struct v4l2_streamparm *sparm)
{
	struct liteon_tof *cam = container_of(subdev,
			struct liteon_tof, subdev);
	struct device *dev = &cam->dev;
	int ret = 0;

	switch (sparm->type) {
	/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		if (sparm->parm.capture.capturemode >= liteon_res_max) {
			dev_warn(dev, "Wrong resolution mode\n");
			ret = -EINVAL;
		}
		cam->cap_parm.capturemode =
			(u32)sparm->parm.capture.capturemode;
		break;
	default:
		dev_warn(dev, "Parameter type is unknown - %d\n", sparm->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int liteon_tof_set_fmt(struct v4l2_subdev *subdev,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct liteon_tof *cam = container_of(subdev, struct liteon_tof, subdev);
	int res;

	if (mf->code != MEDIA_BUS_FMT_SBGGR12_1X12)
		return -EINVAL;

	res = find_resulution(mf->width, mf->height);

	if (res >= 0) {
		cam->cap_parm.capturemode = res;
		return 0;
	}

	return -EINVAL;
}

static int liteon_tof_get_fmt(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct liteon_tof *cam = container_of(subdev, struct liteon_tof,
			subdev);
	int capmode = cam->cap_parm.capturemode;

	if (format->pad)
		return -EINVAL;

	mf->code	= MEDIA_BUS_FMT_SBGGR12_1X12;
	mf->colorspace	= V4L2_COLORSPACE_RAW;
	mf->field	= V4L2_FIELD_NONE;
	mf->width	= liteon_valid_res[capmode].width;
	mf->height	= liteon_valid_res[capmode].height;

	return 0;
}

static int liteon_tof_enum_mbus_code(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SBGGR12_1X12;
	return 0;
}

static int liteon_tof_enum_framesizes(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index > liteon_res_max)
		return -EINVAL;

	fse->max_width = liteon_valid_res[fse->index].width;
	fse->min_width = fse->max_width;
	fse->max_height = liteon_valid_res[fse->index].height;
	fse->min_height = fse->max_height;
	return 0;
}

static int liteon_tof_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{
	return 0;
}

static struct v4l2_subdev_video_ops liteon_tof_subdev_video_ops = {
	.g_parm = liteon_tof_g_parm,
	.s_parm = liteon_tof_s_parm,
};

static const struct v4l2_subdev_pad_ops liteon_tof_subdev_pad_ops = {
	.enum_frame_size       = liteon_tof_enum_framesizes,
	.enum_mbus_code        = liteon_tof_enum_mbus_code,
	.set_fmt               = liteon_tof_set_fmt,
	.get_fmt               = liteon_tof_get_fmt,
};

static struct v4l2_subdev_core_ops liteon_tof_subdev_core_ops = {
	.s_power	= liteon_tof_s_power,
};

static struct v4l2_subdev_ops liteon_tof_subdev_ops = {
	.core	= &liteon_tof_subdev_core_ops,
	.video	= &liteon_tof_subdev_video_ops,
	.pad	= &liteon_tof_subdev_pad_ops,
};

static const struct media_entity_operations liteon_tof_sd_media_ops = {
	.link_setup = liteon_tof_link_setup,
};

static int liteon_tof_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct liteon_tof *cam;
	struct device_node *dn;
	int err;

	cam = devm_kzalloc(dev, sizeof(*cam), GFP_KERNEL);

	dn = of_find_compatible_node(NULL, NULL, "lton,liteon_tof");
	if (dn)
		liteon_valid_res = liteon_tof_valid_res;

	dn = of_find_compatible_node(NULL, NULL, "lton,liteon_tof_vga");
	if (dn)
		liteon_valid_res = liteon_tof_vga_valid_res;

	if (!liteon_valid_res) {
		dev_err(dev, "No compatible device found");
		return -ENODEV;
	}

	liteon_res_max = sizeof(*liteon_valid_res)/sizeof(liteon_valid_res[0]);

	/* Select default pin configuration */
	if (IS_ERR(devm_pinctrl_get_select_default(dev)))
		dev_warn(dev, "error enabling pinctrl configuration\n");

	/* Read power down pin number */
	cam->pwn_gpio = of_get_named_gpio(dev->of_node, "pwn-gpios", 0);

	if (cam->pwn_gpio == -EPROBE_DEFER)
	    return -EPROBE_DEFER;

	if (gpio_is_valid(cam->pwn_gpio)) {

		/* Request PWN gpio */
		err = devm_gpio_request_one(dev, cam->pwn_gpio, GPIOF_IN,
				"liteon_pwn");
		if (err) {
			dev_err(dev, "PWN gpio request failed\n");
			return err;
		}
	} else
		dev_warn(dev, "camera power down pin is not defined");

	/* Read seset pin number */
	cam->rst_gpio = of_get_named_gpio(dev->of_node, "rst-gpios", 0);

	if (cam->rst_gpio == -EPROBE_DEFER)
	    return -EPROBE_DEFER;

	if (gpio_is_valid(cam->rst_gpio)) {

		/* Request RST gpio */
		err = devm_gpio_request_one(dev, cam->rst_gpio, GPIOF_IN,
				"liteon_rst");
		if (err) {
			dev_err(dev, "RST gpio request failed\n");
			return err;
		}
	} else
		dev_warn(dev, "camera reset pin is not defined");

	/* Read camera clock source */
	cam->clk = devm_clk_get(dev, "csi_mclk");

	if (IS_ERR(cam->clk)) {
		/* assuming clock enabled by default */
		cam->clk = NULL;
		dev_warn(dev, "clock configuration is missing or invalid\n");
	} else {
		clk_prepare_enable(cam->clk);
	}

	cam->cap_parm.capturemode = 0;

	cam->dev = pdev->dev;

	/* Enable power and get out of reset */
	liteon_tof_enable_power(cam);

	if (gpio_is_valid(cam->rst_gpio))
		gpio_direction_output(cam->rst_gpio, 0);

	v4l2_subdev_init(&cam->subdev, &liteon_tof_subdev_ops);
	cam->subdev.owner = THIS_MODULE;
	cam->subdev.dev = &pdev->dev;
	snprintf(cam->subdev.name, sizeof(cam->subdev.name), "%s", pdev->name);

	cam->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	cam->subdev.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	cam->mpad.flags = MEDIA_PAD_FL_SOURCE;

	err = media_entity_pads_init(&cam->subdev.entity, 1, &cam->mpad);
	if (err)
		return err;

	cam->subdev.entity.ops = &liteon_tof_sd_media_ops;

	err = v4l2_async_register_subdev(&cam->subdev);
	if (err) {
		dev_err(&cam->dev, "V4L2 subdev register failed, ret=%d\n",
				err);
		media_entity_cleanup(&cam->subdev.entity);
	}
	dev_info(dev, "Liteon cam probed%s",
		(liteon_valid_res == liteon_tof_vga_valid_res) ?
		" (vga mode)" : "");

	platform_set_drvdata(pdev, cam);

	return 0;
}

static int liteon_tof_remove(struct platform_device *pdev)
{
	struct liteon_tof *sensor = platform_get_drvdata(pdev);

	liteon_tof_disable_power(sensor);
	clk_disable_unprepare(sensor->clk);
	v4l2_async_unregister_subdev(&sensor->subdev);

	return 0;
}

static const struct of_device_id liteon_tof_of_match[] = {
	{ .compatible = "lton,liteon_tof", },
	{ .compatible = "lton,liteon_tof_vga", },
	{ }
};

static struct platform_driver liteon_tof_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name  = "liteon_tof",
		.of_match_table = of_match_ptr(liteon_tof_of_match),
	},
	.remove = liteon_tof_remove,
	.probe  = liteon_tof_probe,
};

int __init liteon_tof_init(void)
{
	return platform_driver_register(&liteon_tof_driver);
}
subsys_initcall(liteon_tof_init);
