#include "asm-generic/bitops/non-atomic.h"
#include "asm-generic/gpio.h"
#include "asm/gpio.h"
#include "linux/export.h"
#include "linux/fs.h"
#include "linux/gfp.h"
#include "linux/gpio.h"
#include "linux/input.h"
#include "linux/interrupt.h"
#include "linux/ioport.h"
#include "linux/irqreturn.h"
#include "linux/mod_devicetable.h"
#include "linux/mutex.h"
#include "linux/of_gpio.h"
#include "linux/platform_device.h"
#include "linux/types.h"
#include <linux/device.h>
#include <linux/input.h>
#include <linux/module.h>

struct subsys_input_key_dev {
  dev_t dev_id;
  int irq;
  int gpio;
  struct mutex lock;
  struct input_dev *input_device;
};

static irqreturn_t subsys_input_key_isr(int irq, void *dev_id) {
  struct subsys_input_key_dev *data = (struct subsys_input_key_dev *)dev_id;

  int val;

  mutex_lock(&data->lock);

  val = gpio_get_value(data->gpio);

  if (val == 0) {
    input_report_key(data->input_device, KEY_0, 1);

  } else {
    input_report_key(data->input_device, KEY_0, 0);
  }

  input_sync(data->input_device);

  mutex_unlock(&data->lock);

  return IRQ_HANDLED;
}

static int subsys_input_key_probe(struct platform_device *pdev) {
  struct subsys_input_key_dev *data;
  int ret;

  data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
  if (!data) {
    dev_err(&pdev->dev, "can not alloc prv data\n");
    return -ENOMEM;
  }

  mutex_init(&data->lock);

  /* 分配设置注册 */
  /* 分配 */
  data->input_device = input_allocate_device();
  if (!data->input_device) {
    dev_err(&pdev->dev, "cann not alloc input device");
    return -ENOMEM;
  }

  /* 设置 */
  data->input_device->name = "my_input_gpio_key";
  data->input_device->phys = "gpio-keys/input0";
  /* 总线类型 */
  data->input_device->id.bustype = BUS_HOST;
  data->input_device->id.vendor = 0x0001;
  data->input_device->id.product = 0x0001;
  data->input_device->id.version = 0x0100;

  __set_bit(EV_KEY, data->input_device->evbit);
  __set_bit(EV_SYN, data->input_device->evbit);

  __set_bit(KEY_0, data->input_device->keybit);

  /*  注册 */
  ret = input_register_device(data->input_device);
  if (ret) {
    dev_err(&pdev->dev, "failed to register input device\n");
    input_put_device(data->input_device);
    return ret;
  }

  data->gpio = of_get_named_gpio(pdev->dev.of_node, "input_key", 0);
  if (!gpio_is_valid(data->gpio)) {
    dev_err(&pdev->dev, "can not get gpio source by of\n");
    return -EINVAL;
  }

  ret = devm_gpio_request_one(&pdev->dev, data->gpio, GPIOF_IN,
                              "subsys_input_key_gpio");
  if (ret) {
    dev_err(&pdev->dev, "gpio request failed\n");
    return -EIO;
  }

  data->irq = gpio_to_irq(data->gpio);
  if (data->irq < 0) {
    dev_err(&pdev->dev, "get irq failed\n");
    return -EIO;
  }

  ret = devm_request_irq(&pdev->dev, data->irq, subsys_input_key_isr,
                         IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                         "subsys_input_key", data);
  if (ret) {
    dev_err(&pdev->dev, "get irq failed\n");
    return -EIO;
  }

  platform_set_drvdata(pdev, data);

  dev_info(&pdev->dev, "subsys input keydev driver has been initialized\n");

  return 0;
}

static int subsys_input_key_remove(struct platform_device *pdev) {
  struct subsys_input_key_dev *data = platform_get_drvdata(pdev);

  input_unregister_device(data->input_device);

  dev_info(&pdev->dev, "subsys input keydev driver has been removed\n");
}

static struct of_device_id subsys_input_keydev_of_match_table[] = {
    {.compatible = "alientek_subsys_input_key_probe"},
    {/*sentinel*/},
};

static struct platform_device_id subsys_input_key_ids[] = {
    {"alientek_subsys_input_key_probe", 0},
    {/*sentinel*/},
};

static struct platform_driver subsys_input_keydev_driver = {
    .probe = subsys_input_key_probe,
    .remove = subsys_input_key_remove,
    .driver =
        {
            .name = "alientek_subsys_input_key_probe",
            .of_match_table = subsys_input_keydev_of_match_table,
        },
    .id_table = subsys_input_key_ids,
};

module_platform_driver(subsys_input_keydev_driver);

MODULE_LICENSE("GPL");