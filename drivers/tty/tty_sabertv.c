

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>


#define TV_COMMAND_SET_CHAR 0b00000001
#define TV_COMMAND_ADVANCE  0b00000010
#define TV_COMMAND_PUT_CHAR 0b00000011
#define TV_COMMAND_SET_X    0b00000100
#define TV_COMMAND_SET_Y    0b00000101
#define TV_COMMAND_NEW_LINE 0b00001000

#define TV_COMMAND_GET_X    0b00010100
#define TV_COMMAND_GET_Y    0b00010101

#define TV_OFFSET_CMD 0
#define TV_OFFSET_DATA 4


static void write32(u32 val, void __iomem *addr) {
    iowrite32(val, addr);
}

static void tv_put_char(const unsigned char c) {
    if(c == '\n'){
        write32(TV_COMMAND_NEW_LINE, TV_cmdReg);
    }else{
        write32(c, TV_dataReg);
        write32(TV_COMMAND_PUT_CHAR, TV_cmdReg);
    }
}


/* Examples I used as template/reference
https://www.oreilly.com/library/view/linux-device-drivers/0596005903/ch18.html
https://github.com/jesstess/ldd3-examples/blob/master/examples/tty/tiny_tty.c
*/



#define DRIVER_VERSION "v1.0"
#define DRIVER_AUTHOR "Emily Atlee <linux@emilyatlee.com"
#define DRIVER_DESC "SABER Text-Video Driver"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);

#define SABETV_TTY_MAJOR 240 /* Experimental */


struct sabertv_driver {
    struct tty_struct *tty;
    struct seaphore sem;
    bool open;
    void __iomem *cmdReg;
    void __iomem *dataReg;
};

static struct sabertv_driver *sabertv_instance;


static int sabertv_open(struct tty_struct *tty, struct file *file) {
    struct sabertv_driver *sabertv;

    tty->driver_data = NULL;
    
    /* instantiate sabertv instance */
    sabertv = *sabertv_instance;
    if (sabertv == NULL) {
        sabertv = kmalloc(sizeof(*sabertv), GFP_KERNEL);
        if(!sabertv){
            return -ENOMEM;
        }
        init_MUTEX(&sabertv->sem);
        *sabertv_instance = sabertv;
    }
    /**/

    down(&sabertv->sem);

    /* Connect driver data and tty instances */
    tty->driver_data = sabertv;
    sabertv->tty = tty;

    sabertv->open = true;

    /* init stuff here */
    tv_init();

    up(&sabertv->sem);
    return 0;
}

static void do_close(struct sabertv_driver *sabertv) {
    down(&sabertv->sem);

    if (sabertv->open) {
        sabertv->open = false;
        /* shutdown stuff here */
    }

    up(&sabertv->sem);
}

static void sabertv_close(struct tty_struct *tty, struct file *file) {
    struct sabertv_driver *sabertv = tty->driver_data;

    if (sabertv_driver) {
        do_close(sabertv_driver);
    }
}

static int sabertv_write (struct tty_struct *tty, const unsigned char *buffer, int count) {
    struct sabertv_driver *sabertv = tty->driver_data;
    int i;
    
    if (!sabertv) {
        return -ENODEV;
    }

    down(&sabertv->sem);

    if(sabertv->open){
        for(i = 0; i < count; ++i){
            tv_put_char(buffer[i]);
        }
    }

    up(&sabertv->sem);
}

static int sabertv_write_room (struct tty_struct *tty) {
    struct sabertv_driver *sabertv = tty->driver_data;
    
    if (!sabertv) {
        return -ENODEV;
    }

    down(&sabertv->sem);

    if(sabertv->open){
        return 255; /* arbitrary since the device instantly prints there is always room */
    }

    up(&sabertv->sem);
}


static struct tty_operations serial_ops = {
    .open = sabertv_open,
    .close = sabertv_close,
    .write = sabertv_write,
    .write_room = sabertv_write_room,
    /*.set_termios = sabertv_set_termios, */
};

static const struct of_device_id sabertv_of_match[] = {
	{ .compatible = "saber,tv" },
    {0}
};
MODULE_DEVICE_TABLE(of, sabertv_of_match);


static int sabertv_platform_probe (struct platform_device *pdev) {

    struct resource *res = platform_get_resource(pdev, IORESOURCE_REG, 0);

    void __iomem base = ioremap(res->start, resource_size(res));

    if(sabertv_instance == NULL) { 
        return -ENODEV;
    }

    sabertv_instance->cmdReg = base + TV_OFFSET_CMD;
    sabertv_instance->dataReg = base + TV_OFFSET_DATA;

    return 0;
}

static struct tty_driver *sabertv_tty_driver;

static struct platform_driver sabertv_platform_driver = {
    .probe = sabertv_platform_probe,
    .driver = {
        .name = "saber_tv",
        .owner = THIS_MODULE,
        .of_match_table = sabertv_of_match,
    }
}

static int __init sabertv_init(void)
{
    int ret;

    sabertv_tty_driver = alloc_tty_driver(SABETV_TTY_MINORS);
    if (!sabertv_tty_driver)
        return -ENOMEM;

    sabertv_tty_driver->owner = THIS_MODULE;
    sabertv_tty_driver->driver_name = "saber_tv_tty";
    sabertv_tty_driver->name = "tvtty";
    sabertv_tty_driver->major = SABETV_TTY_MAJOR;
    sabertv_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
    sabertv_tty_driver->subtype = SERIAL_TYPE_NORMAL;
    sabertv_tty_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
    sabertv_tty_driver->init_termios = tty_std_termios;
    sabertv_tty_driver->of_match_table = sabertv_of_match;
    tty_set_operations(sabertv_tty_driver, &serial_ops);

    ret = tty_register_driver(sabertv_tty_driver);
    if(ret){
        printk(KERN_ERR "failed to register saber tv tty driver");
        put_tty_driver(sabertv_tty_driver);
        return ret;
    }

    // Register TTY device driver
    tty_register_device(sabertv_tty_driver, 0, NULL);

    // Register Platform driver for device tree probe
    platform_driver_register(&sabertv_platform_driver);
    
    printk(KERN_INFO DRIVER_DESC " " DRIVER_VERSION);

    return ret;
}

static void __exit sabertv_exit(void)
{
    tty_unregister_device(sabertv_tty_driver, 0);
    tty_register_driver(sabertv_tty_driver);

    if (sabertv) {
        do_close(sabertv);
        kfree(sabertv);
        sabertv = NULL;
    }
}



module_init(sabertv_init);
module_exit(sabertv_exit);

