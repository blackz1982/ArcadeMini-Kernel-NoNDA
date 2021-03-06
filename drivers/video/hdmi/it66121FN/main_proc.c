 /*
 * linux/drivers/power/jz_battery
 *
 * Battery measurement code for Ingenic JZ SOC.
 *
 * based on tosa_battery.c
 *
 * Copyright (C) 2008 Marek Vasut <marek.vasut@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <asm/jzsoc.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>

#include "gpio_i2c.h"

#include "hdmitx.h"

#include "hdmitx_sys.h"

#define INITOK	1
#define INITNO  0
#define DRIVER_NAME	"hdmi"
static unsigned int init_state=INITNO;
static unsigned int ite6610_rate = 200; /* ite6610 scan rate(ms)*/
struct timer_list LoopProc_timer;
static HDMI_Video_Type VideoMode = HDMI_480p60;
//static HDMI_OutputColorMode OutputColorMode = HDMI_YUV444 ;//HDMI_RGB444;
//static HDMI_OutputColorMode OutputColorMode = HDMI_RGB444 ;//HDMI_RGB444;


void hdmi_reset(int is_rest)
{
	if(is_rest)
	{
		__gpio_as_output(HDMI_RST_N_PIN);
		__gpio_set_pin(HDMI_RST_N_PIN);
		__gpio_clear_pin(HDMI_RST_N_PIN);
		//mdelay(30);
	}
	else //normal
		__gpio_set_pin(HDMI_RST_N_PIN);

	printk("GPIO LVL:%d \n",__gpio_get_pin(HDMI_RST_N_PIN));    
}

static void hdmi_power_enable(void)
{
	//__lcd_close_backlight();

	__gpio_as_output(HDMI_POWERON_EN);
	__gpio_set_pin(HDMI_POWERON_EN);

	mdelay(10);
	printk("test hdmi power enable\n");
	
	hdmi_reset(0);
	mdelay(10);
}

static void hdmi_power_disable(void)
{
	__gpio_as_output(HDMI_POWERON_EN);
	__gpio_clear_pin(HDMI_POWERON_EN);

	hdmi_reset(1);
}

static struct timeval tstart,tend;
static int timedif;

static void LoopProc_do_timer(unsigned long arg)
{
	mod_timer(&LoopProc_timer,jiffies+ (ite6610_rate* HZ) / 200);
	IT6610I2cInit();	
	HDMITX_DevLoopProc();
	IT6610I2cDeinit();
//	printk("200m LoopProc_do_timer \n");

#if 0
	do_gettimeofday(&tend) ;
	timedif = 1000000 * (tend.tv_sec-tstart.tv_sec) + tend.tv_usec-tstart.tv_usec;
	printk("kernel run is %d \n",	timedif);
	tstart = tend;
#endif

	return;
}
static void LoopProc_timer_init(void)
{
	init_timer(&LoopProc_timer);
	LoopProc_timer.function=&LoopProc_do_timer;
	LoopProc_timer.expires=jiffies + (ite6610_rate* HZ) / 200; 
	add_timer(&LoopProc_timer);

	do_gettimeofday(&tstart) ;
}

static void LoopProc_timer_finish(void)
{
	del_timer(&LoopProc_timer);
}

static void inline lcd_24bit_as_gpio()		
{				
	REG_GPIO_PXFUNC(2) = 0x0fffffff;	   
	REG_GPIO_PXSELC(2) = 0x0fffffff; 	   
	REG_GPIO_PXDIRS(2) = 0x0fffffff; 	  
	REG_GPIO_PXPES(2)  = 0x0fffffff;	   
	REG_GPIO_PXDATC(2) = 0x0fffffff;	
}


static void it6610_enter_hdmi(unsigned int mode)
{
	__gpio_as_lcd_24bit();//allen add
	
	//HDMITX_ChangeDisplayOption(mode,OutputColorMode);//HDMI_YUV444
	 HDMITX_ChangeDisplayOption(HDMI_640x480p60,HDMI_RGB444) ;//HDMI_RGB444
	
	init_state=INITOK;
	hdmi_power_enable();
	VideoMode = mode;

	IT6610I2cInit();
	
	InitHDMITX_Variable();
	InitHDMITX();

	IT6610I2cDeinit();

	LoopProc_timer_init();
}

static void it6610_exit_hdmi(void)
{
	if(INITOK==init_state)
	{
		printk("go out  it6610  !!!!!!!!!!!!!!!!!!!!!\n");
		mdelay(10);
		LoopProc_timer_finish();
		hdmi_power_disable();
		init_state=INITNO;
		lcd_24bit_as_gpio();
	}
}


static int it6610_open(struct inode *inode, struct file *file)
{
	printk("enter it6610_open ... \n");
	return 0;
}
static int it6610_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static int it6610_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
#define HDMI_POWER_ON		3
#define HDMI_POWER_OFF		4	
	int __user *argp = (int __user *)arg;
	switch(cmd)
	{
		case HDMI_POWER_ON:
		{
			unsigned int SetVal=0;
			if(0!=get_user(SetVal,argp))
			{
				printk("IT6610_SET_VIDEO_MODE get_user error\n");
				return -EFAULT;
			}
			printk("HDMI_POWER_ON VideoMode=%d\n",SetVal);
			it6610_enter_hdmi(SetVal);
			break;
		}
		case HDMI_POWER_OFF:
		     it6610_exit_hdmi();
		     break;
		default:
		     printk("it6610_ioctl Not supported command: 0x%x\n", cmd);
		     break;

	}
	printk("it5510_ioctl cmd %d ...\n",cmd); //wjx
	return 0;
}

static int it6610_mode = 0;
static int proc_it6610_read_proc(char *page,char **start,off_t off,int count,int *eof,void *data)
{
	printk("Medive printk: read it6610 cur mode is %d\n",it6610_mode);
	return sprintf(page,"%1u\n",it6610_mode);
}

static int proc_it6610_write_proc(struct file *file,const char *buffer,unsigned long count,void *data)
{
	unsigned int val ;
	
	val = simple_strtoul(buffer,0,10);
	printk("Medive printk: write it610 mode is %d\n",val);
	if(val == it6610_mode )
		return count;

	it6610_mode = val;
	if (it6610_mode)
		it6610_enter_hdmi(it6610_mode);
	else
		it6610_exit_hdmi();
	
	return count;
}

void power_ctrl_it6610(int val)
{
	it6610_mode = val;
	if (it6610_mode)
		it6610_enter_hdmi(it6610_mode);
	else
		it6610_exit_hdmi();
}


static const struct file_operations it6610_hdmi_fops = {
	.open       = it6610_open,
	.release    = it6610_release,
	.ioctl      = it6610_ioctl,
};

static struct miscdevice it6610_miscdev = {
	.minor          = MISC_DYNAMIC_MINOR,
	.name           = DRIVER_NAME,
//wjx	.name           = "hdmi_ite6610",
	.fops           = &it6610_hdmi_fops,
};
static int __devinit ite6610_drv_probe(struct platform_device *dev)
{
	int ret = misc_register(&it6610_miscdev);
//wjx	__hdmi_power_off();
	printk("ite6610_drv_probe \n");
	if (ret) {
		printk( "cannot register miscdev on minor=%d (%d)\n",-1, ret);
	}

	return 0;
}

static int __devexit ite6610_drv_remove(struct platform_device *dev)
{
	misc_deregister(&it6610_miscdev);
	return 0;
}

static struct platform_driver ite6610_drv_driver = {
	.driver.name	= "ite6610-drv",
	.driver.owner	= THIS_MODULE,
	.probe		= ite6610_drv_probe,
	.remove		= ite6610_drv_remove,
};


static int __init ite6610_drv_init(void)
{
	/* Medive add it6610 proc*/
	struct proc_dir_entry *res;

	res = create_proc_entry("jz/it6610_me",0,NULL);
	if (res)
	{
		printk("Medive printk:  create proc :  it6610_me!\n");
		//res_tvout->owner = THIS_MODULE;
		res->read_proc = proc_it6610_read_proc;
		res->write_proc = proc_it6610_write_proc;
		res->data = NULL;
	}

	return platform_driver_register(&ite6610_drv_driver);
}

static void __exit ite6610_drv_exit(void)
{
	platform_driver_unregister(&ite6610_drv_driver);
}

module_init(ite6610_drv_init);
module_exit(ite6610_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ssg <estt501>");
MODULE_DESCRIPTION("hdmi ite6610 driver");
