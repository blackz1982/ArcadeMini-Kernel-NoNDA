/*
 * linux/arch/mips/jz4760b/board-cygnus.c
 *
 * JZ4760B Cygnus board setup routines.
 *
 * Copyright (c) 2006-2010  Ingenic Semiconductor Inc.
 * Author: <jlwei@ingenic.cn>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/console.h>
#include <linux/delay.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/mipsregs.h>
#include <asm/reboot.h>

#include <asm/jzsoc.h>
#include <linux/i2c.h>
#include <linux/mmc/host.h>
#include <linux/ft5x0x_ts.h>
#include <linux/spi/spi.h>
#include <linux/gpio_keys.h>
#include <linux/gpio_interface.h>
#include <linux/platform_device.h>

extern void (*jz_timer_callback)(void);
extern int __init jz_add_msc_devices(unsigned int controller, struct jz_mmc_platform_data *plat);
extern int __init jz_add_spi_devices(unsigned int host_id, struct spi_board_info *board_info,int board_num);


#undef DEBUG
//#define DEBUG
#ifdef DEBUG
#define dprintk(x...)	printk(x)
#else
#define dprintk(x...)
#endif

/*
 *  config_gpio_on_sleep config all gpio pins when sleep.
 */
struct gpio_sleep_status {
	unsigned int input;
	unsigned int input_pull;
	unsigned int input_no_pull;
	unsigned int output;
	unsigned int output_high;
	unsigned int output_low;
	unsigned int no_operation;
};

void config_gpio_on_sleep(void)
{
	int i = 0;
	struct gpio_sleep_status gpio_sleep_st[] = {
		/* GPA */
		{
			.input_pull = BIT31 | BIT27 |
			BITS_H2L(29,28) | /* NC pin */
			BITS_H2L(19,0), /* NAND: SD0~SD15 */

			.output_high = BIT21 | BIT22 | BIT23 | BIT24 | BIT25 | BIT26, /* NAND: CS1~CS6 */
			.output_low = 0x0,
#ifndef CONFIG_JZ_SYSTEM_AT_CARD
			.no_operation = 0x0,
#else
			.no_operation = BITS_H2L(23, 18),
#endif
		},

		/* GPB */
		{
			.input_pull = BIT30 | BIT27 | BIT26 | BIT25 | BITS_H2L(24,22) | BIT20 |
			BITS_H2L(19,0), /* SA0~SA5 */

			.output_high = BIT29,
			.output_low = BIT31 | BIT28 | BIT21,
#ifndef CONFIG_JZ_SYSTEM_AT_CARD
			.no_operation = 0x0,
#else
			.no_operation = BIT0,
#endif
		},

		/* GPC */
		{
			.input_pull = BITS_H2L(31,28),
			.output_high = 0x0,
			.output_low = BITS_H2L(27,0),
			.no_operation = 0x0,
		},

		/* GPD */
		{
			.input_pull = BITS_H2L(29,26) | BITS_H2L(19,14) | BITS_H2L(13,12) || BITS_H2L(10,0) | BIT11,  // bit11 temporary input_pull
			.output_high = 0x0,
			.output_low =  BITS_H2L(25,20), // | BIT11,
			.no_operation = 0x0,
		},

		/* GPE */
		{
			.input_pull = BITS_H2L(18,11) | BITS_H2L(8,3) | BIT0,
			.output_high = BIT9,
			.output_low = BITS_H2L(29,20) | BIT10 | BIT1 | BIT2,
			.no_operation = 0x0,
		},

		/* GPF */
		{
			.input_pull = BIT11 | BITS_H2L(8,4) | BITS_H2L(2,0),
			.output_high = BIT9,
			.output_low = BIT3,
			.no_operation = 0x0,
		},
	};

	for (i = 0; i < 6; i++) {
		gpio_sleep_st[i].input_pull &= ~gpio_sleep_st[i].no_operation;
		gpio_sleep_st[i].output_high &= ~gpio_sleep_st[i].no_operation;
		gpio_sleep_st[i].output_low &= ~gpio_sleep_st[i].no_operation;
		gpio_sleep_st[i].input_no_pull = 0xffffffff &
			~(gpio_sleep_st[i].input_pull |
			  gpio_sleep_st[i].output_high |
			  gpio_sleep_st[i].output_low) &
			~gpio_sleep_st[i].no_operation;

		gpio_sleep_st[i].input = gpio_sleep_st[i].input_pull | gpio_sleep_st[i].input_no_pull;
		gpio_sleep_st[i].output = gpio_sleep_st[i].output_high | gpio_sleep_st[i].output_low;

		/* all as gpio, except interrupt pins(see @wakeup_key_setup()) */
		REG_GPIO_PXFUNC(i) =  (0xffffffff & ~gpio_sleep_st[i].no_operation);
		REG_GPIO_PXSELC(i) =  (0xffffffff & ~gpio_sleep_st[i].no_operation);
		/* input */
		REG_GPIO_PXDIRC(i) =  gpio_sleep_st[i].input;
		/* pull */
		REG_GPIO_PXPEC(i) = gpio_sleep_st[i].input_pull;
		/* no_pull */
		REG_GPIO_PXPES(i) =  gpio_sleep_st[i].input_no_pull;

		/* output */
		REG_GPIO_PXDIRS(i) =  gpio_sleep_st[i].output;
		REG_GPIO_PXPES(i)  = gpio_sleep_st[i].output; /* disable pull */
		/* high */
		REG_GPIO_PXDATS(i) = gpio_sleep_st[i].output_high;
		/* low */
		REG_GPIO_PXDATC(i) = gpio_sleep_st[i].output_low;
	}
}

struct wakeup_key_s {
	int gpio;       /* gpio pin number */
	int active_low; /* the key interrupt pin is low voltage
                           or fall edge acitve */
};

/* add wakeup keys here */
static struct wakeup_key_s wakeup_key[] = {
	{
		.gpio = GPIO_POWER_ON,
		.active_low = ACTIVE_LOW_WAKE_UP,
	},
#ifndef CONFIG_JZ_SYSTEM_AT_CARD
	{
		.gpio = MSC0_HOTPLUG_PIN,
		.active_low = ACTIVE_LOW_MSC0_CD,
	},
#endif
	{
		.gpio = MSC2_HOTPLUG_PIN,
		.active_low = ACTIVE_LOW_MSC2_CD,
	},
};

static void wakeup_key_setup(void)
{
	int i;
	int num = sizeof(wakeup_key) / sizeof(wakeup_key[0]);

	for(i = 0; i < num; i++) {
		if(wakeup_key[i].active_low)
			__gpio_as_irq_fall_edge(wakeup_key[i].gpio);
		else
			__gpio_as_irq_rise_edge(wakeup_key[i].gpio);

		__gpio_ack_irq(wakeup_key[i].gpio);
		__gpio_unmask_irq(wakeup_key[i].gpio);
		__intc_unmask_irq(IRQ_GPIO0 - (wakeup_key[i].gpio/32));  /* unmask IRQ_GPIOn */
	}
}


/* NOTES:
 * 1: Pins that are floated (NC) should be set as input and pull-enable.
 * 2: Pins that are pull-up or pull-down by outside should be set as input
 *    and pull-disable.
 * 3: Pins that are connected to a chip except sdram and nand flash
 *    should be set as input and pull-disable, too.
 */
void jz_board_do_sleep(unsigned long *ptr)
{
	unsigned char i;

        /* Print messages of GPIO registers for debug */
	for(i=0;i<GPIO_PORT_NUM;i++) {
		dprintk("run dat:%x pin:%x fun:%x sel:%x dir:%x pull:%x msk:%x trg:%x\n",        \
			REG_GPIO_PXDAT(i),REG_GPIO_PXPIN(i),REG_GPIO_PXFUN(i),REG_GPIO_PXSEL(i), \
			REG_GPIO_PXDIR(i),REG_GPIO_PXPE(i),REG_GPIO_PXIM(i),REG_GPIO_PXTRG(i));
	}

        /* Save GPIO registers */
	for(i = 1; i < GPIO_PORT_NUM; i++) {
		*ptr++ = REG_GPIO_PXFUN(i);
		*ptr++ = REG_GPIO_PXSEL(i);
		*ptr++ = REG_GPIO_PXDIR(i);
		*ptr++ = REG_GPIO_PXPE(i);
		*ptr++ = REG_GPIO_PXIM(i);
		*ptr++ = REG_GPIO_PXDAT(i);
		*ptr++ = REG_GPIO_PXTRG(i);
	}

        /*
         * Set all pins to pull-disable, and set all pins as input except
         * sdram and the pins which can be used as CS1_N to CS4_N for chip select.
         */
	config_gpio_on_sleep();

        /*
	 * Set proper status for GPC21 to GPC24 which can be used as CS1_N to CS4_N.
	 * Keep the pins' function used for chip select(CS) here according to your
         * system to avoid chip select crashing with sdram when resuming from sleep mode.
         */

	/*
         * If you must set some GPIOs as output to high level or low level,
         * you can set them here, using:
         * __gpio_as_output(n);
         * __gpio_set_pin(n); or  __gpio_clear_pin(n);
	 */

	if (!console_suspend_enabled)
		__gpio_as_uart1();

#if 0
        /* Keep uart function for printing debug message */
	__gpio_as_uart0();
	__gpio_as_uart1();
	__gpio_as_uart2();
	__gpio_as_uart3();

        /* Print messages of GPIO registers for debug */
	for(i=0;i<GPIO_PORT_NUM;i++) {
		printk("GP%d: data:0x%08x pin:0x%08x fun:0x%08x sel:0x%08x dir:0x%08x pull:0x%08x msk:0x%08x trg:0x%08x\n",
		       i, REG_GPIO_PXDAT(i),REG_GPIO_PXPIN(i),REG_GPIO_PXFUN(i),REG_GPIO_PXSEL(i),
		       REG_GPIO_PXDIR(i),REG_GPIO_PXPE(i),REG_GPIO_PXIM(i),REG_GPIO_PXTRG(i));
	}
#endif
	wakeup_key_setup();
}

void jz_board_do_resume(unsigned long *ptr)
{
	unsigned char i;

	/* Restore GPIO registers */
	for(i = 1; i < GPIO_PORT_NUM; i++) {
		 REG_GPIO_PXFUNS(i) = *ptr;
		 REG_GPIO_PXFUNC(i) = ~(*ptr++);

		 REG_GPIO_PXSELS(i) = *ptr;
		 REG_GPIO_PXSELC(i) = ~(*ptr++);

		 REG_GPIO_PXDIRS(i) = *ptr;
		 REG_GPIO_PXDIRC(i) = ~(*ptr++);

		 REG_GPIO_PXPES(i) = *ptr;
		 REG_GPIO_PXPEC(i) = ~(*ptr++);

		 REG_GPIO_PXIMS(i)=*ptr;
		 REG_GPIO_PXIMC(i)=~(*ptr++);

		 REG_GPIO_PXDATS(i)=*ptr;
		 REG_GPIO_PXDATC(i)=~(*ptr++);

		 REG_GPIO_PXTRGS(i)=*ptr;
		 REG_GPIO_PXTRGC(i)=~(*ptr++);
	}

        /* Print messages of GPIO registers for debug */
	for(i=0;i<GPIO_PORT_NUM;i++) {
		dprintk("resume dat:%x pin:%x fun:%x sel:%x dir:%x pull:%x msk:%x trg:%x\n",     \
			REG_GPIO_PXDAT(i),REG_GPIO_PXPIN(i),REG_GPIO_PXFUN(i),REG_GPIO_PXSEL(i), \
			REG_GPIO_PXDIR(i),REG_GPIO_PXPE(i),REG_GPIO_PXIM(i),REG_GPIO_PXTRG(i));
	}
}

static void lepus_sd_gpio_init(struct device *dev)
{
#if defined (CONFIG_JZ_SYSTEM_AT_CARD)
	__gpio_as_msc0_boot();
#else
	__gpio_as_msc0_4bit();
	__gpio_as_output(GPIO_SD0_VCC_EN_N);
	__gpio_set_pin(GPIO_SD0_VCC_EN_N); /* poweroff */
	__gpio_as_input(GPIO_SD0_CD_N);
#endif
}

static void lepus_sd_power_on(struct device *dev)
{
#ifndef CONFIG_JZ_SYSTEM_AT_CARD
	__msc0_enable_power();
#endif
}

static void lepus_sd_power_off(struct device *dev)
{
#ifndef CONFIG_JZ_SYSTEM_AT_CARD
	__msc0_disable_power();
#endif
}

static void lepus_sd_cpm_start(struct device *dev)
{
	cpm_start_clock(CGM_MSC0);
}

static unsigned int lepus_sd_status(struct device *dev)
{
#if defined(CONFIG_JZ_SYSTEM_AT_CARD)
	return 1;
#else
	unsigned int status;

	status = (unsigned int) __gpio_get_pin(GPIO_SD0_CD_N);
#if ACTIVE_LOW_MSC0_CD
	return !status;
#else
	return status;
#endif
#endif
}

static void lepus_sd_plug_change(int state)
{
#if defined(CONFIG_JZ_SYSTEM_AT_CARD)
	return;
#endif
	if(state == CARD_INSERTED) /* wait for remove */
#if ACTIVE_LOW_MSC0_CD
		__gpio_as_irq_high_level(MSC0_HOTPLUG_PIN);
#else
		__gpio_as_irq_low_level(MSC0_HOTPLUG_PIN);
#endif
	else		      /* wait for insert */
#if ACTIVE_LOW_MSC0_CD
		__gpio_as_irq_low_level(MSC0_HOTPLUG_PIN);
#else
		__gpio_as_irq_high_level(MSC0_HOTPLUG_PIN);
#endif
}

static unsigned int lepus_sd_get_wp(struct device *dev)
{
#if defined(CONFIG_JZ_SYSTEM_AT_CARD)
	return 0;
#else
	int i = 0;
	unsigned int status;

	status = (unsigned int) __gpio_get_pin(MSC0_WP_PIN);
	return (status);
#endif
}
#if defined(CONFIG_JZ_SYSTEM_AT_CARD)
struct mmc_partition_info lepus_partitions[] = {
	[0] = {"mbr",0,512,0},//0 - 512
	[1] = {"uboot",512,3*1024*1024-512,0}, // 512 - 2.5MB
	[2] = {"misc",0x300000,0x100000,0},//3MB - 1MB
	[3] = {"kernel",0x400000,0x400000,0},//4MB - 4MB
	
	[4] = {"rootfs",8*1024*1024,350*1024*1024,1}, //250MB
	[5] = {"data",358*1024*1024,100*1024*1024,1},  //90MB app
	[6] = {"cache",458*1024*1024,30*1024*1024,1}, //30MB cfg
	[7] = {"test_0",488*1024*1024,14352*1024*1024UL,0},//4G is 3737 16G=14840
//	[8] = {"test_0",0x0,0xffffffff,0},


};
#endif

struct jz_mmc_platform_data lepus_sd_data = {
#ifndef CONFIG_JZ_MSC0_SDIO_SUPPORT
	.support_sdio   = 0,
#else
	.support_sdio   = 1,
#endif
	.ocr_mask	= MMC_VDD_32_33 | MMC_VDD_33_34,
#if defined(CONFIG_JZ_SYSTEM_AT_CARD)
	.status_irq	= 0,
	.detect_pin     = 0,
#else
	.status_irq	= MSC0_HOTPLUG_IRQ,
	.detect_pin     = GPIO_SD0_CD_N,
#endif
	.init           = lepus_sd_gpio_init,
	.power_on       = lepus_sd_power_on,
	.power_off      = lepus_sd_power_off,
	.cpm_start	= lepus_sd_cpm_start,
	.status		= lepus_sd_status,
	.plug_change	= lepus_sd_plug_change,
	.write_protect  = lepus_sd_get_wp,
	.max_bus_width  = MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED
#ifdef CONFIG_JZ_MSC0_BUS_4
	| MMC_CAP_4_BIT_DATA
#endif
#ifdef CONFIG_JZ_MSC0_BUS_8
	| MMC_CAP_8_BIT_DATA
#endif
	,

#ifdef CONFIG_JZ_MSC0_BUS_1
	.bus_width      = 1,
#elif defined  CONFIG_JZ_MSC0_BUS_4
	.bus_width      = 4,
#else
	.bus_width      = 8,
#endif
#if defined(CONFIG_JZ_SYSTEM_AT_CARD)
	.partitions = lepus_partitions,
	.num_partitions = ARRAY_SIZE(lepus_partitions),
	.permission = MMC_BOOT_AREA_PROTECTED,
#endif
};

static void lepus_tf_gpio_init(struct device *dev)
{
	__gpio_e_as_msc2_4bit();
	
	__gpio_as_output(GPIO_SD2_VCC_EN_N);
	__gpio_set_pin(GPIO_SD2_VCC_EN_N); /* poweroff */
	
	__gpio_as_input(GPIO_SD2_CD_N);
	__gpio_enable_pull(GPIO_SD2_CD_N);//allen
}

static void lepus_tf_power_on(struct device *dev)
{
	__gpio_e_as_msc2_4bit();

	__msc2_enable_power();
	//printk(" msc1 open power !\n");
}

static void lepus_tf_power_off(struct device *dev)
{
	//__msc2_disable_power(); //allen del
}

static void lepus_tf_cpm_start(struct device *dev)
{
	cpm_start_clock(CGM_MSC2);
}

static unsigned int lepus_tf_status(struct device *dev)
{
	unsigned int status = 0;

	__gpio_enable_pull(GPIO_SD2_CD_N);//allen add

	status = (unsigned int) __gpio_get_pin(GPIO_SD2_CD_N);
	//printk("--------------- lepus_tf_status is %d \n",status);
#if ACTIVE_LOW_MSC2_CD
	return !status;
#else
	return status;
#endif
}

static void lepus_tf_plug_change(int state)
{
	__gpio_enable_pull(GPIO_SD2_CD_N);//allen add
	
	if(state == CARD_INSERTED) /* wait for remove */
#if ACTIVE_LOW_MSC2_CD
		__gpio_as_irq_high_level(MSC2_HOTPLUG_PIN);
#else
		__gpio_as_irq_low_level(MSC2_HOTPLUG_PIN);
#endif
	else		      /* wait for insert */
#if ACTIVE_LOW_MSC2_CD
		__gpio_as_irq_low_level(MSC2_HOTPLUG_PIN);
#else
		__gpio_as_irq_high_level(MSC2_HOTPLUG_PIN);
#endif
}

struct jz_mmc_platform_data lepus_tf_data = {
#ifndef CONFIG_JZ_MSC2_SDIO_SUPPORT
	.support_sdio   = 0,
#else
	.support_sdio   = 1,
#endif
	.ocr_mask	= MMC_VDD_32_33 | MMC_VDD_33_34,
	.status_irq	    = MSC2_HOTPLUG_IRQ,
	.detect_pin     = GPIO_SD2_CD_N,
	.init           = lepus_tf_gpio_init,
	.power_on       = lepus_tf_power_on,
	.power_off      = lepus_tf_power_off,
	.cpm_start	= lepus_tf_cpm_start,
	.status		= lepus_tf_status,
	.plug_change	= lepus_tf_plug_change,
	.max_bus_width  = MMC_CAP_SD_HIGHSPEED
#ifdef CONFIG_JZ_MSC2_BUS_4
	| MMC_CAP_4_BIT_DATA
#endif
	,
#ifdef CONFIG_JZ_MSC2_BUS_1
	.bus_width      = 1,
#else
	.bus_width      = 4,
#endif
};

static void lepus_msc1_gpio_init(struct device *dev)
{
	__gpio_as_msc1_4bit();
	__gpio_as_output(GPIO_SD1_VCC_EN_N);
	__gpio_set_pin(GPIO_SD1_VCC_EN_N); /* poweroff */
	
	__gpio_as_input(GPIO_SD1_CD_N);
	__gpio_enable_pull(GPIO_SD1_CD_N);//allen
	return;
}

static void lepus_msc1_power_on(struct device *dev)
{
	__gpio_as_msc1_4bit();
	
	__msc1_enable_power();
	return;
}

static void lepus_msc1_power_off(struct device *dev)
{
	//__msc1_disable_power();
	return;
}

static void lepus_msc1_cpm_start(struct device *dev)
{
	cpm_start_clock(CGM_MSC1);
}

static unsigned int lepus_msc1_status(struct device *dev)
{
	unsigned int status = 0;
	
	__gpio_enable_pull(GPIO_SD1_CD_N);//allen add

	status = (unsigned int) __gpio_get_pin(GPIO_SD1_CD_N);
	//printk("--------------- lepus_tf_status is %d \n",status);
#if ACTIVE_LOW_MSC1_CD
	return !status;
#else
	return status;
#endif
}

static void lepus_msc1_plug_change(int state)
{
	__gpio_enable_pull(GPIO_SD1_CD_N);//allen add
	
	if(state == CARD_INSERTED) /* wait for remove */
#if ACTIVE_LOW_MSC1_CD
		__gpio_as_irq_high_level(MSC1_HOTPLUG_PIN);
#else
		__gpio_as_irq_low_level(MSC1_HOTPLUG_PIN);
#endif
	else		      /* wait for insert */
#if ACTIVE_LOW_MSC1_CD
		__gpio_as_irq_low_level(MSC1_HOTPLUG_PIN);
#else
		__gpio_as_irq_high_level(MSC1_HOTPLUG_PIN);
#endif
}


struct jz_mmc_platform_data lepus_msc1_data = {
#ifndef CONFIG_JZ_MSC1_SDIO_SUPPORT
	.support_sdio   = 0,
#else
	.support_sdio   = 1,
#endif
	.ocr_mask	= MMC_VDD_32_33 | MMC_VDD_33_34,
	.status_irq	= MSC1_HOTPLUG_IRQ,
	.detect_pin     = GPIO_SD1_CD_N,
	.init           = lepus_msc1_gpio_init,
	.power_on       = lepus_msc1_power_on,
	.power_off      = lepus_msc1_power_off,
	.cpm_start	= lepus_msc1_cpm_start,
	.status		= lepus_msc1_status,
	.plug_change	= lepus_msc1_plug_change,
	.max_bus_width  = MMC_CAP_SD_HIGHSPEED
#ifdef CONFIG_JZ_MSC1_BUS_4
	| MMC_CAP_4_BIT_DATA
#endif
	,
#ifdef CONFIG_JZ_MSC1_BUS_1
	.bus_width      = 1,
#else
	.bus_width      = 4,
#endif
};

void __init board_msc_init(void)
{
	jz_add_msc_devices(0, &lepus_sd_data);
	jz_add_msc_devices(2, &lepus_tf_data);
//	jz_add_msc_devices(1, &lepus_msc1_data);
}

static void f4760b_timer_callback(void)
{
	static unsigned long count = 0;

	if ((++count) % 50 == 0) {
//		dancing();
		count = 0;
	}
}

static void __init board_cpm_setup(void)
{
	/* Stop unused module clocks here.
	 * We have started all module clocks at arch/mips/jz4760b/setup.c.
	 */
}

static struct i2c_board_info lepus_i2c0_devs[] __initdata = {

#ifdef CONFIG_GC0309
	{
		I2C_BOARD_INFO("gc0309",0x21),
	},
#endif
#ifdef CONFIG_GC0328
	{
		I2C_BOARD_INFO("gc0328",0x21),
	},
#endif

#ifdef CONFIG_MY704
	{
		I2C_BOARD_INFO("hi704",0x30),
	},
#endif

};

static struct i2c_board_info lepus_i2c1_devs[] __initdata = { 
	{
		I2C_BOARD_INFO("fm5807_i2c",0x11),   //0x11
	},


};


/* SPI devices */
struct spi_board_info lepus_spi0_board_info[]  = {
	[0] = {
		.modalias       = "spidev0",
		.bus_num        = SPI0_BUS,
		.chip_select    = SPI_CHIPSELECT_NUM_A,
		.max_speed_hz   = 120000,
	},
	[1] = {
		.modalias       = "spitest",
		.bus_num        = SPI0_BUS,
		.chip_select    = SPI_CHIPSELECT_NUM_B,
		.max_speed_hz   = 2500000,
	},
};
struct spi_board_info lepus_spi1_board_info[]  = {
	[0] = {
		.modalias       = "spidev1",
		.bus_num        = SPI1_BUS,
		.chip_select    = SPI_CHIPSELECT_NUM_C,
		.max_speed_hz   = 12000000,
	},
};

void __init board_i2c_init(void) {
	i2c_register_board_info(0, lepus_i2c0_devs, ARRAY_SIZE(lepus_i2c0_devs));
	i2c_register_board_info(1, lepus_i2c1_devs, ARRAY_SIZE(lepus_i2c1_devs));
}
void __init board_spi_init(void){
	//jz_add_spi_devices(0,lepus_spi0_board_info,ARRAY_SIZE(lepus_spi0_board_info));
	//jz_add_spi_devices(1,lepus_spi1_board_info,ARRAY_SIZE(lepus_spi1_board_info));
}
void __init jz_board_setup(void)
{
	printk("JZ4760B Lepus board setup\n");
	board_cpm_setup();
	

	jz_timer_callback = f4760b_timer_callback;
}

#if 0 //allen add for key wake up
static struct gpio_keys_button board_buttons[] = {
	{
		.gpio           = GPIO_POWER_ON,
		.code           = KEY_1,
		.desc           = "KEY_1",
		.active_low     = ACTIVE_LOW,
	},
};
static struct gpio_keys_platform_data board_button_data = {
	.buttons        = board_buttons,
	.nbuttons       = ARRAY_SIZE(board_buttons),
};

static struct platform_device board_button_device = {
	.name           = "jz-gpio-key",
	.id             = -1,
	.num_resources  = 0,
	.dev            = {
		.platform_data  = &board_button_data,
	}
};


static int __init lepus_board_init(void)
{                                                                              
	platform_device_register(&board_button_device);
	
	return 0;
}

arch_initcall(lepus_board_init);

#endif


/**
 * Called by arch/mips/kernel/proc.c when 'cat /proc/cpuinfo'.
  */
const char *get_board_type(void)
{
	return "Lepus";
}


