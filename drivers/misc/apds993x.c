/*
 * apds993x.c - Linux kernel modules for ambient light + proximity sensor
 *
 * Copyright (C) 2012 Lee Kai Koon <kai-koon.lee@avagotech.com>
 * Copyright (C) 2012 Avago Technologies
 * Copyright (C) 2013 LGE Inc.
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
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/i2c/apds993x.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <linux/async.h>
#include <linux/wakelock.h>
#include <linux/spinlock.h>

#define LGE_PROXIMITY_NAME	"lge_proximity"

#define APDS993X_DRV_NAME	"apds993x"
#define DRIVER_VERSION		"1.0.0"

#define ABS_LIGHT	0x29	/* added to support LIGHT - light sensor */

#define ALS_POLLING_ENABLED
#define APDS993X_PM_IRQ_SYNC

#define APDS993X_HAL_USE_SYS_ENABLE

#define APDS993X_PS_DETECTION_THRESHOLD		800
#define APDS993X_PS_HSYTERESIS_THRESHOLD	700
#define APDS993X_PS_PULSE_NUMBER		8

#define APDS993X_ALS_THRESHOLD_HSYTERESIS	20	/* % */

#define APDS993X_GA	48	/* 0.48 without glass window */
#define APDS993X_COE_B	223	/* 2.23 without glass window */
#define APDS993X_COE_C	70	/* 0.70 without glass window */
#define APDS993X_COE_D	142	/* 1.42 without glass window */
#define APDS993X_DF	52

/* Change History
 *
 * 1.0.0	Fundamental Functions of APDS-993x
 *
 */
#define APDS993X_IOCTL_PS_ENABLE	1
#define APDS993X_IOCTL_PS_GET_ENABLE	2
#define APDS993X_IOCTL_PS_GET_PDATA	3	/* pdata */
#define APDS993X_IOCTL_ALS_ENABLE	4
#define APDS993X_IOCTL_ALS_GET_ENABLE	5
#define APDS993X_IOCTL_ALS_GET_CH0DATA	6	/* ch0data */
#define APDS993X_IOCTL_ALS_GET_CH1DATA	7	/* ch1data */
#define APDS993X_IOCTL_ALS_DELAY	8

/*
 * Defines
 */
#define APDS993X_ENABLE_REG	0x00
#define APDS993X_ATIME_REG	0x01
#define APDS993X_PTIME_REG	0x02
#define APDS993X_WTIME_REG	0x03
#define APDS993X_AILTL_REG	0x04
#define APDS993X_AILTH_REG	0x05
#define APDS993X_AIHTL_REG	0x06
#define APDS993X_AIHTH_REG	0x07
#define APDS993X_PILTL_REG	0x08
#define APDS993X_PILTH_REG	0x09
#define APDS993X_PIHTL_REG	0x0A
#define APDS993X_PIHTH_REG	0x0B
#define APDS993X_PERS_REG	0x0C
#define APDS993X_CONFIG_REG	0x0D
#define APDS993X_PPCOUNT_REG	0x0E
#define APDS993X_CONTROL_REG	0x0F
#define APDS993X_REV_REG	0x11
#define APDS993X_ID_REG		0x12
#define APDS993X_STATUS_REG	0x13
#define APDS993X_CH0DATAL_REG	0x14
#define APDS993X_CH0DATAH_REG	0x15
#define APDS993X_CH1DATAL_REG	0x16
#define APDS993X_CH1DATAH_REG	0x17
#define APDS993X_PDATAL_REG	0x18
#define APDS993X_PDATAH_REG	0x19

#define CMD_BYTE		0x80
#define CMD_WORD		0xA0
#define CMD_SPECIAL		0xE0

#define CMD_CLR_PS_INT		0xE5
#define CMD_CLR_ALS_INT		0xE6
#define CMD_CLR_PS_ALS_INT	0xE7


/* Register Value define : ATIME */
#define APDS993X_100MS_ADC_TIME	0xDB  /* 100.64ms integration time */
#define APDS993X_50MS_ADC_TIME	0xED  /* 51.68ms integration time */
#define APDS993X_27MS_ADC_TIME	0xF6  /* 27.2ms integration time */

/* Register Value define : PRXCNFG */
#define APDS993X_ALS_REDUCE	0x04  /* ALSREDUCE - ALS Gain reduced by 4x */

/* Register Value define : PERS */
#define APDS993X_PPERS_0	0x00  /* Every proximity ADC cycle */
#define APDS993X_PPERS_1	0x10  /* 1 consecutive proximity value out of range */
#define APDS993X_PPERS_2	0x20  /* 2 consecutive proximity value out of range */
#define APDS993X_PPERS_3	0x30  /* 3 consecutive proximity value out of range */
#define APDS993X_PPERS_4	0x40  /* 4 consecutive proximity value out of range */
#define APDS993X_PPERS_5	0x50  /* 5 consecutive proximity value out of range */
#define APDS993X_PPERS_6	0x60  /* 6 consecutive proximity value out of range */
#define APDS993X_PPERS_7	0x70  /* 7 consecutive proximity value out of range */
#define APDS993X_PPERS_8	0x80  /* 8 consecutive proximity value out of range */
#define APDS993X_PPERS_9	0x90  /* 9 consecutive proximity value out of range */
#define APDS993X_PPERS_10	0xA0  /* 10 consecutive proximity value out of range */
#define APDS993X_PPERS_11	0xB0  /* 11 consecutive proximity value out of range */
#define APDS993X_PPERS_12	0xC0  /* 12 consecutive proximity value out of range */
#define APDS993X_PPERS_13	0xD0  /* 13 consecutive proximity value out of range */
#define APDS993X_PPERS_14	0xE0  /* 14 consecutive proximity value out of range */
#define APDS993X_PPERS_15	0xF0  /* 15 consecutive proximity value out of range */

#define APDS993X_APERS_0	0x00  /* Every ADC cycle */
#define APDS993X_APERS_1	0x01  /* 1 consecutive proximity value out of range */
#define APDS993X_APERS_2	0x02  /* 2 consecutive proximity value out of range */
#define APDS993X_APERS_3	0x03  /* 3 consecutive proximity value out of range */
#define APDS993X_APERS_5	0x04  /* 5 consecutive proximity value out of range */
#define APDS993X_APERS_10	0x05  /* 10 consecutive proximity value out of range */
#define APDS993X_APERS_15	0x06  /* 15 consecutive proximity value out of range */
#define APDS993X_APERS_20	0x07  /* 20 consecutive proximity value out of range */
#define APDS993X_APERS_25	0x08  /* 25 consecutive proximity value out of range */
#define APDS993X_APERS_30	0x09  /* 30 consecutive proximity value out of range */
#define APDS993X_APERS_35	0x0A  /* 35 consecutive proximity value out of range */
#define APDS993X_APERS_40	0x0B  /* 40 consecutive proximity value out of range */
#define APDS993X_APERS_45	0x0C  /* 45 consecutive proximity value out of range */
#define APDS993X_APERS_50	0x0D  /* 50 consecutive proximity value out of range */
#define APDS993X_APERS_55	0x0E  /* 55 consecutive proximity value out of range */
#define APDS993X_APERS_60	0x0F  /* 60 consecutive proximity value out of range */

/* Register Value define : CONTROL */
#define APDS993X_AGAIN_1X	0x00  /* 1X ALS GAIN */
#define APDS993X_AGAIN_8X	0x01  /* 8X ALS GAIN */
#define APDS993X_AGAIN_16X	0x02  /* 16X ALS GAIN */
#define APDS993X_AGAIN_120X	0x03  /* 120X ALS GAIN */

#define APDS993X_PRX_IR_DIOD	0x20  /* Proximity uses CH1 diode */

#define APDS993X_PGAIN_1X	0x00  /* PS GAIN 1X */
#define APDS993X_PGAIN_2X	0x04  /* PS GAIN 2X */
#define APDS993X_PGAIN_4X	0x08  /* PS GAIN 4X */
#define APDS993X_PGAIN_8X	0x0C  /* PS GAIN 8X */

#define APDS993X_PDRVIE_100MA	0x00  /* PS 100mA LED drive */
#define APDS993X_PDRVIE_50MA	0x40  /* PS 50mA LED drive */
#define APDS993X_PDRVIE_25MA	0x80  /* PS 25mA LED drive */
#define APDS993X_PDRVIE_12_5MA	0xC0  /* PS 12.5mA LED drive */

/*calibration*/
#define DEFAULT_CROSS_TALK	400
#define ADD_TO_CROSS_TALK	300
#define SUB_FROM_PS_THRESHOLD	100

#ifdef APDS993X_PM_IRQ_SYNC
#define APDS993X_STATUS_RESUME      0
#define APDS993X_STATUS_SUSPEND     1
#define APDS993X_STATUS_QUEUE_WORK	2
#endif

/*PS tuning value*/
static int apds993x_ps_detection_threshold = 0;
static int apds993x_ps_hsyteresis_threshold = 0;
static int apds993x_ps_pulse_number = 0;
static int apds993x_ps_pgain = 0;

typedef enum
{
	APDS993X_ALS_RES_10240 = 0,    /* 27.2ms integration time */
	APDS993X_ALS_RES_19456 = 1,    /* 51.68ms integration time */
	APDS993X_ALS_RES_37888 = 2     /* 100.64ms integration time */
} apds993x_als_res_e;

typedef enum
{
	APDS993X_ALS_GAIN_1X    = 0,    /* 1x AGAIN */
	APDS993X_ALS_GAIN_8X    = 1,    /* 8x AGAIN */
	APDS993X_ALS_GAIN_16X   = 2,    /* 16x AGAIN */
	APDS993X_ALS_GAIN_120X  = 3     /* 120x AGAIN */
} apds993x_als_gain_e;

/*
 * Structs
 */
struct apds993x_data {
	struct i2c_client *client;
	struct mutex update_lock;
	struct mutex op_lock;
	struct delayed_work	dwork;		/* for PS interrupt */
	struct delayed_work	als_dwork;	/* for ALS polling */
	struct input_dev *input_dev_als;
	struct input_dev *input_dev_ps;

	struct apds993x_platform_data *platform_data;
	int irq;

	unsigned int enable;
	unsigned int atime;
	unsigned int ptime;
	unsigned int wtime;
	unsigned int ailt;
	unsigned int aiht;
	unsigned int pilt;
	unsigned int piht;
	unsigned int pers;
	unsigned int config;
	unsigned int ppcount;
	unsigned int control;

	/* control flag from HAL */
	unsigned int enable_ps_sensor;
	unsigned int enable_als_sensor;

	/* PS parameters */
	unsigned int ps_threshold;
	unsigned int ps_hysteresis_threshold; 	/* always lower than ps_threshold */
	unsigned int ps_detection;		/* 5 = near-to-far; 0 = far-to-near */
	unsigned int ps_data;			/* to store PS data */

	/*calibration*/
	unsigned int cross_talk;		/* cross_talk value */
	unsigned int avg_cross_talk;		/* average cross_talk  */
	unsigned int ps_cal_result;		/* result of calibration*/

	/* ALS parameters */
	unsigned int als_threshold_l;	/* low threshold */
	unsigned int als_threshold_h;	/* high threshold */
	unsigned int als_data;		/* to store ALS data */
	int als_prev_lux;		/* to store previous lux value */

	unsigned int als_gain;		/* needed for Lux calculation */
	unsigned int als_poll_delay;	/* needed for light sensor polling : micro-second (us) */
	unsigned int als_atime_index;	/* storage for als integratiion time */
	unsigned int als_again_index;	/* storage for als GAIN */
	unsigned int als_reduce;	/* flag indicate ALS 6x reduction */
//	spinlock_t lock;
#ifdef APDS993X_PM_IRQ_SYNC
	struct wake_lock wakelock;
	atomic_t status;
#endif
};

/*
 * Global data
 */
static struct apds993x_data *pdev_data = NULL;

/* global i2c_client to support ioctl */
static struct i2c_client *apds993x_i2c_client = NULL;
static struct workqueue_struct *apds993x_workqueue = NULL;

static unsigned char apds993x_als_atime_tb[] = { 0xF6, 0xED, 0xDB };
static unsigned short apds993x_als_integration_tb[] = {2720, 5168, 10064};
static unsigned short apds993x_als_res_tb[] = { 10240, 19456, 37888 };
static unsigned char apds993x_als_again_tb[] = { 1, 8, 16, 120 };
static unsigned char apds993x_als_again_bit_tb[] = { 0x00, 0x01, 0x02, 0x03 };

/*calibration*/
static int apds993x_cross_talk_val = 0;

/* ALS tuning */
static int apds993x_ga = 0;
static int apds993x_coe_b = 0;
static int apds993x_coe_c = 0;
static int apds993x_coe_d = 0;

static int check_sunlight = 0;
static int forced_far = 0;

#ifdef ALS_POLLING_ENABLED
static int apds993x_set_als_poll_delay(struct i2c_client *client, unsigned int val);
#endif

#define APDS900_SENSOR_DEBUG 1
#if APDS900_SENSOR_DEBUG
struct debug_reg {
	char  *name;
	u8  reg;
};

#define APDS993x_DEBUG_REG(x) {#x, x}

/* you can add register addr below if you want to see it's value
   note) many register to be added below can cause a kernel panic.  */
static struct debug_reg apds993x_debug_regs[] = {
	APDS993x_DEBUG_REG(APDS993X_ENABLE_REG),
	APDS993x_DEBUG_REG(APDS993X_ATIME_REG),
	APDS993x_DEBUG_REG(APDS993X_PTIME_REG),
	APDS993x_DEBUG_REG(APDS993X_WTIME_REG),
	APDS993x_DEBUG_REG(APDS993X_AILTL_REG),
	APDS993x_DEBUG_REG(APDS993X_AILTH_REG),
	APDS993x_DEBUG_REG(APDS993X_AIHTL_REG),
	APDS993x_DEBUG_REG(APDS993X_AIHTH_REG),
	APDS993x_DEBUG_REG(APDS993X_PILTL_REG),
	APDS993x_DEBUG_REG(APDS993X_PILTH_REG),
	APDS993x_DEBUG_REG(APDS993X_PIHTL_REG),
	APDS993x_DEBUG_REG(APDS993X_PIHTH_REG),
	APDS993x_DEBUG_REG(APDS993X_PERS_REG),
	APDS993x_DEBUG_REG(APDS993X_CONFIG_REG),
	APDS993x_DEBUG_REG(APDS993X_PPCOUNT_REG),
	APDS993x_DEBUG_REG(APDS993X_CONTROL_REG),
	APDS993x_DEBUG_REG(APDS993X_REV_REG),
	APDS993x_DEBUG_REG(APDS993X_ID_REG),
	APDS993x_DEBUG_REG(APDS993X_STATUS_REG),
	APDS993x_DEBUG_REG(APDS993X_CH0DATAL_REG),
	APDS993x_DEBUG_REG(APDS993X_CH0DATAH_REG),
	APDS993x_DEBUG_REG(APDS993X_CH1DATAL_REG),
	APDS993x_DEBUG_REG(APDS993X_CH1DATAH_REG),
	APDS993x_DEBUG_REG(APDS993X_PDATAL_REG),
	APDS993x_DEBUG_REG(APDS993X_PDATAH_REG),
};

static int apds993x_get_register_dump(struct i2c_client *client){
	int i;
	int ret = 0;
	u8 regdata = 0;
//	struct apds993x_data *data = i2c_get_clientdata(client);

	printk("\n");
	printk("avago reg dump start ==============================================\n");
	printk("\n");

	for (i = 0 ; i < ARRAY_SIZE(apds993x_debug_regs) ; i++) {
	//	char *name = apds993x_debug_regs[i].name;
	//	u32 reg = apds993x_debug_regs[i].reg;

		regdata = i2c_smbus_read_byte_data(client, CMD_BYTE|apds993x_debug_regs[i].reg);

		if(ret){
			printk("avago register dump fail : %s[0x%x]\n", apds993x_debug_regs[i].name, regdata);
			break;
		}
		printk("avago register dump : %s[0x%x]\n", apds993x_debug_regs[i].name, regdata);
		mdelay(6);
	};

	printk("\n");
	printk("avago reg dump end ================================================\n");
	printk("\n");
	return ret;
};
#endif

/*
 * Management functions
 */
static int apds993x_set_command(struct i2c_client *client, int command)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret = 0;
	int clearInt;

	if (command == 0)
		clearInt = CMD_CLR_PS_INT;
	else if (command == 1)
		clearInt = CMD_CLR_ALS_INT;
	else
		clearInt = CMD_CLR_PS_ALS_INT;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte(client, clearInt);
	mutex_unlock(&data->update_lock);

	return ret;
}

static int apds993x_set_enable(struct i2c_client *client, int enable)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret = 0; 

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte_data(client,
			CMD_BYTE|APDS993X_ENABLE_REG, enable);
	mutex_unlock(&data->update_lock);
	data->enable = enable;

	return ret;
}

static int apds993x_set_atime(struct i2c_client *client, int atime)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret = 0;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte_data(client,
			CMD_BYTE|APDS993X_ATIME_REG, atime);
	mutex_unlock(&data->update_lock);

	data->atime = atime;

	return ret;
}

static int apds993x_set_ptime(struct i2c_client *client, int ptime)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret = 0;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte_data(client,
			CMD_BYTE|APDS993X_PTIME_REG, ptime);
	mutex_unlock(&data->update_lock);

	data->ptime = ptime;

	return ret;
}

static int apds993x_set_wtime(struct i2c_client *client, int wtime)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret = 0;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte_data(client,
			CMD_BYTE|APDS993X_WTIME_REG, wtime);
	mutex_unlock(&data->update_lock);

	data->wtime = wtime;

	return ret;
}

static int apds993x_set_ailt(struct i2c_client *client, int threshold)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_word_data(client,
			CMD_WORD|APDS993X_AILTL_REG, threshold);
	mutex_unlock(&data->update_lock);

	data->ailt = threshold;

	return ret;
}

static int apds993x_set_aiht(struct i2c_client *client, int threshold)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_word_data(client,
			CMD_WORD|APDS993X_AIHTL_REG, threshold);
	mutex_unlock(&data->update_lock);

	data->aiht = threshold;

	return ret;
}

static int apds993x_set_pilt(struct i2c_client *client, int threshold)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_word_data(client,
			CMD_WORD|APDS993X_PILTL_REG, threshold);
	mutex_unlock(&data->update_lock);

	data->pilt = threshold;

	return ret;
}

static int apds993x_set_piht(struct i2c_client *client, int threshold)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_word_data(client,
			CMD_WORD|APDS993X_PIHTL_REG, threshold);
	mutex_unlock(&data->update_lock);

	data->piht = threshold;

	return ret;
}

static int apds993x_set_pers(struct i2c_client *client, int pers)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte_data(client,
			CMD_BYTE|APDS993X_PERS_REG, pers);
	mutex_unlock(&data->update_lock);

	data->pers = pers;

	return ret;
}

static int apds993x_set_config(struct i2c_client *client, int config)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte_data(client,
			CMD_BYTE|APDS993X_CONFIG_REG, config);
	mutex_unlock(&data->update_lock);

	data->config = config;

	return ret;
}

static int apds993x_set_ppcount(struct i2c_client *client, int ppcount)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte_data(client,
			CMD_BYTE|APDS993X_PPCOUNT_REG, ppcount);
	mutex_unlock(&data->update_lock);

	data->ppcount = ppcount;

	return ret;
}

static int apds993x_set_control(struct i2c_client *client, int control)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte_data(client,
			CMD_BYTE|APDS993X_CONTROL_REG, control);
	mutex_unlock(&data->update_lock);

	data->control = control;

	return ret;
}

/*calibration*/
void apds993x_swap(int *x, int *y)
{
	int temp = *x;
	*x = *y;
	*y = temp;
}

static int apds993x_run_cross_talk_calibration(struct i2c_client *client)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	unsigned int sum_of_pdata = 0;
	unsigned int temp_pdata[20];
	unsigned int ArySize = 20;
	unsigned int cal_check_flag = 0;
	int i, j;
	unsigned int old_enable = 0;
//	int ret = 0;
	old_enable = data->enable;
#if defined(APDS993x_SENSOR_DEBUG)
	int status;
	int rdata;
#endif
	pr_info("%s: START proximity sensor calibration\n", __func__);

RECALIBRATION:
	sum_of_pdata = 0;
	//apds993x_init_client(client);
	apds993x_set_enable(client, 0x0D);/* Enable PS and Wait */

#if defined(APDS993x_SENSOR_DEBUG)
	mutex_lock(&data->update_lock);
	status = i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_STATUS_REG);
	rdata = i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_ENABLE_REG);
	mutex_unlock(&data->update_lock);

	pr_info("%s: APDS993x_ENABLE_REG=%2d APDS993x_STATUS_REG=%2d\n",
			__func__, rdata, status);
#endif

	for (i = 0; i < 20; i++) {
		mdelay(6);
		mutex_lock(&data->update_lock);
		temp_pdata[i] = i2c_smbus_read_word_data(client,
				CMD_WORD|APDS993X_PDATAL_REG);
		mutex_unlock(&data->update_lock);
	}

	/* pdata sorting */
	for (i = 0; i < ArySize - 1; i++)
		for (j = i+1; j < ArySize; j++)
			if (temp_pdata[i] > temp_pdata[j])
				apds993x_swap(temp_pdata + i, temp_pdata + j);

	/* calculate the cross-talk using central 10 data */
	for (i = 5; i < 15; i++) {
		pr_info("%s: temp_pdata = %d\n", __func__, temp_pdata[i]);
		sum_of_pdata = sum_of_pdata + temp_pdata[i];
	}

	data->cross_talk = sum_of_pdata/10;
	pr_info("%s: sum_of_pdata = %d   cross_talk = %d\n",
			__func__, sum_of_pdata, data->cross_talk);

	/*
	 * this value is used at Hidden Menu to check
	 * if the calibration is pass or fail
	 */
	data->avg_cross_talk = data->cross_talk;

	if (data->cross_talk > 720) {
		pr_warn("%s: invalid calibrated data\n", __func__);

		if (cal_check_flag == 0) {
			pr_info("%s: RECALIBRATION start\n", __func__);
			cal_check_flag = 1;
			goto RECALIBRATION;
		} else {
			pr_err("%s: CALIBRATION FAIL -> "
					"cross_talk is set to DEFAULT\n", __func__);
			data->cross_talk = DEFAULT_CROSS_TALK;
			apds993x_set_enable(client, 0x00); /* Power Off */
			data->ps_cal_result = 0; /* 0:Fail, 1:Pass */
			return -EINVAL;
		}
	}

	data->ps_threshold = ADD_TO_CROSS_TALK + data->cross_talk;
	data->ps_hysteresis_threshold =
		data->ps_threshold - SUB_FROM_PS_THRESHOLD;

	apds993x_set_enable(client, 0x00); /* Power Off */
	data->ps_cal_result = 1;
	pr_info("%s: total_pdata = %d & cross_talk = %d\n",
			__func__, sum_of_pdata, data->cross_talk);
	pr_info("%s: FINISH proximity sensor calibration\n", __func__);

	/* Save the cross-talk to the non-volitile memory in the phone  */
	return data->cross_talk;
}

/* apply the Cross-talk value to threshold */
static void apds993x_set_ps_threshold_adding_cross_talk(
		struct i2c_client *client, unsigned int cal_data)
{
	struct apds993x_data *data = i2c_get_clientdata(client);

	if (cal_data > 770)
		cal_data = 770;
	if (cal_data < 0)
		cal_data = 0;

	if (cal_data == 0) {
		data->ps_threshold = apds993x_ps_detection_threshold;
		data->ps_hysteresis_threshold =
			data->ps_threshold - SUB_FROM_PS_THRESHOLD;
	} else {
		data->cross_talk = cal_data;
		data->ps_threshold = ADD_TO_CROSS_TALK + data->cross_talk;
		data->ps_hysteresis_threshold =
			data->ps_threshold - SUB_FROM_PS_THRESHOLD;
	}
	pr_info("%s: configurations are set\n", __func__);
}

static int LuxCalculation(struct i2c_client *client, int ch0data, int ch1data)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int luxValue=0;
	int IAC1=0;
	int IAC2=0;
	int IAC=0;

	if ((ch0data >= apds993x_als_res_tb[data->als_atime_index] ||
			ch1data >= apds993x_als_res_tb[data->als_atime_index]) && (data->als_reduce)) {
		luxValue = 30*1000;
		return luxValue;
	}

	/* re-adjust COE_B to avoid 2 decimal point */
	IAC1 = (ch0data - (apds993x_coe_b * ch1data) / 100);
	/* re-adjust COE_C and COE_D to void 2 decimal point */
	IAC2 = ((apds993x_coe_c * ch0data) / 100 -
			(apds993x_coe_d * ch1data) / 100);

	if (IAC1 > IAC2)
		IAC = IAC1;
	else if (IAC1 <= IAC2)
		IAC = IAC2;
	else
		IAC = 0;

	if (IAC1 < 0 && IAC2 < 0) {
		if (ch0data < apds993x_als_res_tb[data->als_atime_index]/2) {
			IAC = 0;
		} else {
			luxValue = 30*1000;
			return -1;	// Don't report max lux, reduce gain may help
		}
	}

	if (data->als_reduce) {
		luxValue = ((IAC * apds993x_ga * APDS993X_DF) / 100) * 65 / 10 /
			((apds993x_als_integration_tb[data->als_atime_index] /
			  100) * apds993x_als_again_tb[data->als_again_index]);
	} else {
		luxValue = ((IAC * apds993x_ga * APDS993X_DF) /100) /
			((apds993x_als_integration_tb[data->als_atime_index] /
			  100) * apds993x_als_again_tb[data->als_again_index]);
	}

	return luxValue;
}

static void apds993x_change_ps_threshold(struct i2c_client *client)
{
	struct apds993x_data *data = i2c_get_clientdata(client);

	apds993x_set_pers(client, 0x33);    // 29-Feb-2012 KK
	// repeat this because of the force first interrupt
	data->ps_data =	i2c_smbus_read_word_data(
			client, CMD_WORD|APDS993X_PDATAL_REG);

	if ((data->ps_data > data->pilt) && (data->ps_data >= data->piht)) {
		/* far-to-near detected */
		data->ps_detection = 1;

		/* FAR-to-NEAR detection */
		pr_info("%s: FAR-TO-NEAR data->ps_data = %d, data->pilt = %d, data->piht = %d\n",
				__func__, data->ps_data, data->pilt, data->piht);

		input_report_abs(data->input_dev_ps, ABS_DISTANCE, 0);
		input_sync(data->input_dev_ps);

		if(!forced_far){
			apds993x_set_pilt(client, data->ps_hysteresis_threshold);
			apds993x_set_piht(client, 1023);
		}else{
			apds993x_set_pilt(client, 500);
			apds993x_set_piht(client, 1023);
			forced_far = 0;
		}

	} else if ((data->ps_data <= data->pilt) &&
			(data->ps_data < data->piht)) {
		/* near-to-far detected */
		data->ps_detection = 0;

		/* NEAR-to-FAR detection */
		input_report_abs(data->input_dev_ps, ABS_DISTANCE, 5);
		input_sync(data->input_dev_ps);

		apds993x_set_pilt(client, 0);
		apds993x_set_piht(client, data->ps_threshold);

		pr_info("%s: near-to-far\n", __func__);
	}
	else if ( (data->pilt == 1023) && (data->piht == 0) )
	{
#if 1 // proximity: to decide first value whether NEAR or FAR
		pr_info("%s: >>>> ps_threshold 0 \n", __func__);

		if (data->ps_data > data->ps_threshold) {
			/* far-to-near detected */
			data->ps_detection = 1;

			/* FAR-to-NEAR detection */
			input_report_abs(data->input_dev_ps, ABS_DISTANCE, 0);
			input_sync(data->input_dev_ps);

			apds993x_set_pilt(client, data->ps_hysteresis_threshold);
			apds993x_set_piht(client, 1023);

			pr_info("%s: far-to-near\n", __func__);
		} else if (data->ps_data <= data->ps_threshold) {
			/* near-to-far detected */
			data->ps_detection = 0;

			/* NEAR-to-FAR detection */
			input_report_abs(data->input_dev_ps, ABS_DISTANCE, 5);
			input_sync(data->input_dev_ps);

			apds993x_set_pilt(client, 0);
			apds993x_set_piht(client, data->ps_threshold);

			pr_info("%s: near-to-far\n", __func__);
		}
#else
		/* this is the first near-to-far forced interrupt */
		data->ps_detection = 0;
		printk(">>>>>>>>>> ps_threshold 4 \n");

		input_report_abs(data->input_dev_ps, ABS_DISTANCE, 5);/* NEAR-to-FAR detection */
		input_sync(data->input_dev_ps);

		apds993x_set_pilt(client, 0);
		apds993x_set_piht(client, data->ps_threshold);

		printk("\n near-to-FAR detected\n\n");
#endif
	}
}

static void apds993x_change_als_threshold(struct i2c_client *client)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ch0data, ch1data, v;
	int luxValue=0;
	int err;
	unsigned char change_again=0;
	unsigned char control_data=0;
	unsigned char lux_is_valid=1;

	ch0data = i2c_smbus_read_word_data(client,
			CMD_WORD|APDS993X_CH0DATAL_REG);
	ch1data = i2c_smbus_read_word_data(client,
			CMD_WORD|APDS993X_CH1DATAL_REG);

	luxValue = LuxCalculation(client, ch0data, ch1data);

	if (luxValue >= 0) {
		luxValue = (luxValue < 30000) ? luxValue : 30000;
		data->als_prev_lux = luxValue;
	} else {
		/* don't report, the lux is invalid value */
		lux_is_valid = 0;
		luxValue = data->als_prev_lux;
		if (data->als_reduce) {
			lux_is_valid = 1;
			/* report anyway since this is the lowest gain */
			luxValue = 30000;
		}
	}

	/*
	   pr_info("%s: lux=%d ch0data=%d ch1data=%d again=%d als_reduce=%d\n",
	   __func__,
	   luxValue, ch0data, ch1data,
	   apds993x_als_again_tb[data->als_again_index],
	   data->als_reduce);
	 */

	/*
	 *  check PS under sunlight
	 * PS was previously in far-to-near condition
	 */
	v = 1024 * (256 - apds993x_als_atime_tb[data->als_atime_index]);
	v = (v * 75) / 100;
	if ((data->ps_detection == 1) && (ch0data > v)) {
		/*
		 * need to inform input event as there will be no interrupt
		 * from the PS
		 */
		/* NEAR-to-FAR detection */
		input_report_abs(data->input_dev_ps, ABS_DISTANCE, 5);
		input_sync(data->input_dev_ps);

		apds993x_set_pilt(client, 0);
		apds993x_set_piht(client, data->ps_threshold);

		/* near-to-far detected */
		data->ps_detection = 0;

		pr_info("%s: FAR\n", __func__);
	}

	if (lux_is_valid) {
		/* report the lux level */
		input_report_abs(data->input_dev_als, ABS_LIGHT, luxValue);
		input_sync(data->input_dev_als);
	}

	data->als_data = ch0data;

	data->als_threshold_l = (data->als_data *
			(100 - APDS993X_ALS_THRESHOLD_HSYTERESIS)) / 100;
	data->als_threshold_h = (data->als_data *
			(100 + APDS993X_ALS_THRESHOLD_HSYTERESIS)) / 100;

	if (data->als_threshold_h >=
			apds993x_als_res_tb[data->als_atime_index]) {
		data->als_threshold_h =
			apds993x_als_res_tb[data->als_atime_index];
	}

	if (data->als_data >=
			((apds993x_als_res_tb[data->als_atime_index] * 90 ) / 100)) {
		/* lower AGAIN if possible */
		if (data->als_again_index != APDS993X_ALS_GAIN_1X) {
			data->als_again_index--;
			change_again = 1;
		} else {
			err = i2c_smbus_write_byte_data(client,
					CMD_BYTE|APDS993X_CONFIG_REG,
					APDS993X_ALS_REDUCE);
			if (err >= 0)
				data->als_reduce = 1;
		}
	} else if (data->als_data <=
			((apds993x_als_res_tb[data->als_atime_index] * 10) / 100)) {
		/* increase AGAIN if possible */
		if (data->als_reduce) {
			err = i2c_smbus_write_byte_data(client,
					CMD_BYTE|APDS993X_CONFIG_REG, 0);
			if (err >= 0)
				data->als_reduce = 0;
		} else if (data->als_again_index != APDS993X_ALS_GAIN_120X) {
			data->als_again_index++;
			change_again = 1;
		}
	}

	if (change_again) {
		control_data = i2c_smbus_read_byte_data(client,
				CMD_BYTE|APDS993X_CONTROL_REG);
		control_data = control_data & 0xFC;
		control_data = control_data |
			apds993x_als_again_bit_tb[data->als_again_index];
		i2c_smbus_write_byte_data(client,
				CMD_BYTE|APDS993X_CONTROL_REG, control_data);
	}

	i2c_smbus_write_word_data(client,
			CMD_WORD|APDS993X_AILTL_REG, data->als_threshold_l);
	i2c_smbus_write_word_data(client,
			CMD_WORD|APDS993X_AIHTL_REG, data->als_threshold_h);

}

static void apds993x_reschedule_work(struct apds993x_data *data,
		unsigned long delay)
{
	/*
	 * If work is already scheduled then subsequent schedules will not
	 * change the scheduled time that's why we have to cancel it first.
	 */

//	spin_lock(&data->lock);

	cancel_delayed_work(&data->dwork);
	queue_delayed_work(apds993x_workqueue, &data->dwork, delay);

//	spin_unlock(&data->lock);
}


#ifdef ALS_POLLING_ENABLED
/* ALS polling routine */
static void apds993x_als_polling_work_handler(struct work_struct *work)
{
	struct apds993x_data *data = container_of(work,
			struct apds993x_data, als_dwork.work);
	struct i2c_client *client=data->client;
	int ch0data, ch1data, pdata;
	int luxValue=0;
	int err;
	unsigned char change_again=0;
	unsigned char control_data=0;
	unsigned char lux_is_valid=1;

	if(data->enable_als_sensor == 0)
	{
		cancel_delayed_work(&data->als_dwork);
		pr_info("%s: als sensor already disabled... Exit w/o Queueing... \n", __func__);
		return;
	}

	ch0data = i2c_smbus_read_word_data(client,
			CMD_WORD|APDS993X_CH0DATAL_REG);
	ch1data = i2c_smbus_read_word_data(client,
			CMD_WORD|APDS993X_CH1DATAL_REG);
	pdata = i2c_smbus_read_word_data(client,
			CMD_WORD|APDS993X_PDATAL_REG);

	luxValue = LuxCalculation(client, ch0data, ch1data);

	if (luxValue >= 0) {
		luxValue = luxValue<30000 ? luxValue : 30000;
		data->als_prev_lux = luxValue;
	} else {
		/* don't report, this is invalid lux value */
		lux_is_valid = 0;
		luxValue = data->als_prev_lux;
		if (data->als_reduce) {
			lux_is_valid = 1;
			/* report anyway since this is the lowest gain */
			luxValue = 30000;
		}
	}

	   pr_debug("%s: lux=%d ch0data=%d ch1data=%d pdata=%d delay=%d again=%d "
	   "als_reduce=%d)\n",
	   __func__,
	   luxValue, ch0data, ch1data, pdata,
	   data->als_poll_delay,
	   apds993x_als_again_tb[data->als_again_index],
	   data->als_reduce);


#if 0
	/*
	 * check PS under sunlight
	 * PS was previously in far-to-near condition
	 */
	v = (75 * (1024 * (256 - data->atime))) / 100;
	if ((data->ps_detection == 1) && (ch0data > v)) {
		/*
		 * need to inform input event as there will be no interrupt
		 * from the PS
		 */
		/* NEAR-to-FAR detection */
		input_report_abs(data->input_dev_ps, ABS_DISTANCE, 5);
		input_sync(data->input_dev_ps);

		apds993x_set_pilt(client, 0);
		apds993x_set_piht(client, data->ps_threshold);

		data->ps_detection = 0;	/* near-to-far detected */

		pr_info("%s: FAR\n", __func__);
	}
#endif

	if (lux_is_valid) {
		/* report the lux level */
		input_report_abs(data->input_dev_als, ABS_LIGHT, luxValue);
		input_sync(data->input_dev_als);
	}

	data->als_data = ch0data;

	if (data->als_data >=
			(apds993x_als_res_tb[data->als_atime_index]* 90) / 100) {
		/* lower AGAIN if possible */
		if (data->als_again_index != APDS993X_ALS_GAIN_1X) {
			data->als_again_index--;
			change_again = 1;
		} else {
			err = i2c_smbus_write_byte_data(client,
					CMD_BYTE|APDS993X_CONFIG_REG,
					APDS993X_ALS_REDUCE);
			if (err >= 0)
				data->als_reduce = 1;
		}
	} else if (data->als_data <=
			(apds993x_als_res_tb[data->als_atime_index] * 10) / 100) {
		/* increase AGAIN if possible */
		if (data->als_reduce) {
			err = i2c_smbus_write_byte_data(client,
					CMD_BYTE|APDS993X_CONFIG_REG, 0);
			if (err >= 0)
				data->als_reduce = 0;
		} else if (data->als_again_index != APDS993X_ALS_GAIN_120X) {
			data->als_again_index++;
			change_again = 1;
		}
	}

	if (change_again) {
		control_data = i2c_smbus_read_byte_data(client,
				CMD_BYTE|APDS993X_CONTROL_REG);
		control_data = control_data & 0xFC;
		control_data = control_data |
			apds993x_als_again_bit_tb[data->als_again_index];
		i2c_smbus_write_byte_data(client,
				CMD_BYTE|APDS993X_CONTROL_REG, control_data);
	}

	/* restart timer */
#ifdef APDS993X_PM_IRQ_SYNC
	if(atomic_read(&data->status) != APDS993X_STATUS_SUSPEND) {
		queue_delayed_work(apds993x_workqueue,
				&data->als_dwork, msecs_to_jiffies(data->als_poll_delay));
	}
#endif

}
#endif /* ALS_POLLING_ENABLED */

/* PS interrupt routine */
static void apds993x_work_handler(struct work_struct *work)
{
	struct apds993x_data *data =
		container_of(work, struct apds993x_data, dwork.work);
	struct i2c_client *client=data->client;
	int status;
	int ch0data;
	int enable;

#ifdef APDS993X_PM_IRQ_SYNC
	if(wake_lock_active(&data->wakelock))
		wake_unlock(&data->wakelock);

	wake_lock_timeout(&data->wakelock, 2 * HZ);
#endif

	status = i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_STATUS_REG);
	if(status < 0)
	{
            pr_info("%s: i2c_smbus_read_byte_data APDS993X_STATUS_REG error\n",__func__);
            return;
	}
	enable = i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_ENABLE_REG);
	if(enable < 0)
	{
            pr_info("%s: i2c_smbus_read_byte_data APDS993X_ENABLE_REG error\n",__func__);
            return;
	}

	/* disable 993x's ADC first */
	i2c_smbus_write_byte_data(client, CMD_BYTE|APDS993X_ENABLE_REG, 1);

//	pr_debug("%s: status = 0x%x, enable: 0x%x, ch0data: %d\n", __func__, status, enable, ch0data);

	if ((status & enable & 0x30) == 0x30) {
		/* both PS and ALS are interrupted */
		apds993x_change_als_threshold(client);

		ch0data = i2c_smbus_read_word_data(client,
				CMD_WORD|APDS993X_CH0DATAL_REG);
		if (ch0data < (75 * (1024 * (256 - data->atime))) / 100) {
			apds993x_change_ps_threshold(client);
		} else {
			if (data->ps_detection == 1)
				apds993x_change_ps_threshold(client);
			else
				pr_info("%s: background ambient noise\n",	__func__);
		}

		/* 2 = CMD_CLR_PS_ALS_INT */
		apds993x_set_command(client, 2);
	} else if ((status & enable & 0x20) == 0x20) {
		/* only PS is interrupted */

		/* check if this is triggered by background ambient noise */
		ch0data = i2c_smbus_read_word_data(client,
				CMD_WORD|APDS993X_CH0DATAL_REG);

		pr_debug("%s : 1111 adps993x kangsik  ch0data = %d, data->ps_detection = %d, check_sunlight = %d\n",
					__func__, ch0data, data->ps_detection, check_sunlight);
		pr_debug("%s : apds993x  kangsik ch0data limit = %d,  psat = %d\n", __func__, 75 * (apds993x_als_res_tb[data->als_atime_index]) / 100, check_sunlight);
		if (ch0data <
				(75 * (apds993x_als_res_tb[data->als_atime_index])) / 100) {
			apds993x_change_ps_threshold(client);
		} else {
			/* ps saturation */
			if((status & 0x40) == 0x40) {
				check_sunlight = 1;
			}
			pr_debug("%s: apds993x  2222 kangsik ch0data limit = %d,  psat = %d\n", __func__, 75 * (apds993x_als_res_tb[data->als_atime_index]) / 100, check_sunlight);

			if (data->ps_detection == 1){ // near -> far
				apds993x_change_ps_threshold(client);
			}
			else{ // far -> near
				pr_info("%s: background ambient noise\n", __func__);
				data->ps_data =	i2c_smbus_read_word_data(client, CMD_WORD|APDS993X_PDATAL_REG);

				pr_debug("%s: adps993x kangsik  data->ps_data = %d, data->pilt = %d, data->piht = %d\n",
					__func__, data->ps_data, data->pilt, data->piht);

				if(check_sunlight){
					/* NEAR-to-FAR detection */
					data->ps_detection = 0;
					input_report_abs(data->input_dev_ps, ABS_DISTANCE, 5);
					input_sync(data->input_dev_ps);

					apds993x_set_pilt(client, 500); // 0
					apds993x_set_piht(client, 1023); // data->ps_threshold
					forced_far = 1;
					pr_info("%s: apds993x forced NEAR_TO_FAR\n", __func__);
				}
				pr_debug("%s: 2222 adps993x kangsik  ch0data = %d, data->ps_detection = %d, check_sunlight = %d\n",
					__func__, ch0data, data->ps_detection, check_sunlight);
			}
			check_sunlight = 0;
		}

		/* 0 = CMD_CLR_PS_INT */
		apds993x_set_command(client, 0);
	} else if ((status & enable & 0x10) == 0x10) {
		/* only ALS is interrupted */
		apds993x_change_als_threshold(client);

		/* 1 = CMD_CLR_ALS_INT */
		apds993x_set_command(client, 1);
	} else {	// unknown state but clear interrupt
		apds993x_set_command(client, 0);
	}

	i2c_smbus_write_byte_data(client,
			CMD_BYTE|APDS993X_ENABLE_REG, data->enable);
}

/* assume this is ISR */
static irqreturn_t apds993x_interrupt(int vec, void *info)
{
	struct i2c_client *client=(struct i2c_client *)info;
	struct apds993x_data *data = i2c_get_clientdata(client);

	pr_info("%s: apds993x_interrupt!\n", __func__);

#ifdef APDS993X_PM_IRQ_SYNC
	if(wake_lock_active(&data->wakelock))
		wake_unlock(&data->wakelock);

	wake_lock_timeout(&data->wakelock, msecs_to_jiffies(500));

	if(atomic_read(&data->status) == APDS993X_STATUS_SUSPEND) {
		atomic_set(&data->status, APDS993X_STATUS_QUEUE_WORK);
	} else {
		apds993x_reschedule_work(data, 0);
	}
#else
	apds993x_reschedule_work(data, 0);
#endif

	return IRQ_HANDLED;
}

/*
 * IOCTL support
 */
static int apds993x_enable_als_sensor(struct i2c_client *client, int val)
{
	struct apds993x_data *data = i2c_get_clientdata(client);

	pr_info("%s: val=%d\n", __func__, val);

	if ((val != 0) && (val != 1)) {
		pr_err("%s: invalid value (val = %d)\n", __func__, val);
		return -EINVAL;
	}

	mutex_lock(&data->op_lock);

	if (val == 1) {
		/* turn on light  sensor */
		if (data->enable_als_sensor == 0) {
			data->enable_als_sensor = 1;
			/* Power Off */
			if(apds993x_set_enable(client,0)<0) {
#ifdef CONFIG_APDS9930_DEBUG
			pr_err("%s: apds993x_set_enable_0x00\n", __func__);
#endif
                           goto out;
                        }

#ifdef ALS_POLLING_ENABLED
			if (data->enable_ps_sensor) {
				/* Enable PS with interrupt */
				if(apds993x_set_enable(client, 0x27) < 0) {
#ifdef CONFIG_APDS9930_DEBUG
				pr_err("%s: apds993x_set_enable_0x27\n", __func__);
#endif
                                   goto out;
                                }
			} else {
				/* no interrupt*/
				if(apds993x_set_enable(client, 0x03) < 0) {
#ifdef CONFIG_APDS9930_DEBUG
				pr_err("%s: apds993x_set_enable_0x03\n", __func__);
#endif
                                  goto out;
                                }
			}
#else
			/*
			 *  force first ALS interrupt in order to
			 * get environment reading
			 */
			apds993x_set_ailt( client, 0xFFFF);
#ifdef CONFIG_APDS9930_DEBUG
			pr_info("%s: apds993x_set_ailt 0xFFFF\n", __func__);
#endif
			apds993x_set_aiht( client, 0);
#ifdef CONFIG_APDS9930_DEBUG
			pr_info("%s: apds993x_set_aiht 0x00\n", __func__);
#endif

			if (data->enable_ps_sensor) {
				/* Enable both ALS and PS with interrupt */
				if(apds993x_set_enable(client, 0x37) < 0) {
#ifdef CONFIG_APDS9930_DEBUG
				pr_err("%s: apds993x_set_enable_0x37\n", __func__);
#endif
                                  goto out;
                                }
			} else {
				/* only enable light sensor with interrupt*/
				if(apds993x_set_enable(client, 0x13) < 0) {
#ifdef CONFIG_APDS9930_DEBUG
				pr_err("%s: apds993x_set_enable_0x13\n", __func__);
#endif
                                  goto out;
                                }
			}
#endif

		/* to send lux initial value to hal */
		input_report_abs(data->input_dev_als, ABS_LIGHT, 30001);
		input_sync(data->input_dev_als);

#ifdef ALS_POLLING_ENABLED
			/*
			 * If work is already scheduled then subsequent
			 * schedules will not change the scheduled time
			 * that's why we have to cancel it first.
			 */
			if(!cancel_delayed_work(&data->als_dwork)) {
			    flush_delayed_work(&data->als_dwork);
                        }
			queue_delayed_work(apds993x_workqueue, &data->als_dwork, 0);
#endif
		}
	} else {
		/*
		 * turn off light sensor
		 * what if the p sensor is active?
		 */
		data->enable_als_sensor = 0;

		if (data->enable_ps_sensor == 1) {
			/* Power Off */
			if(apds993x_set_enable(client,0) < 0) {
#ifdef CONFIG_APDS9930_DEBUG
			pr_err("%s: apds993x_set_enable 0x00\n", __func__);
#endif
                          goto out;
                        }
#if 0 // APDS9930 als/proxy bug fix on calling
			apds993x_set_piht(client, 0);
			apds993x_set_piht(client,
					apds993x_ps_detection_threshold);
#endif
			/* only enable prox sensor with interrupt */
			if(apds993x_set_enable(client, 0x27) < 0){
#ifdef CONFIG_APDS9930_DEBUG
			pr_err("%s: apds993x_set_enable 0x27\n", __func__);
#endif
                          goto out;
                         }
		} else {
			if(apds993x_set_enable(client, 0) < 0) {
#ifdef CONFIG_APDS9930_DEBUG
			pr_err("%s: apds993x_set_enable 0x00\n", __func__);
#endif
                          goto out;
                        }
		}

#ifdef ALS_POLLING_ENABLED
		/*
		 * If work is already scheduled then subsequent schedules
		 * will not change the scheduled time that's why we have
		 * to cancel it first.
		 */
		if(!cancel_delayed_work(&data->als_dwork)) {
		    flush_delayed_work(&data->als_dwork);
                }
#endif
	}
	mutex_unlock(&data->op_lock);
	pr_info("%s: apds993x_enable_als_sensor_end\n", __func__);
	return 0;

out:
	mutex_unlock(&data->op_lock);
        printk("%s: als_set_enable... io error\n", __func__);
	return -1;
}

#ifdef ALS_POLLING_ENABLED
static int apds993x_set_als_poll_delay(struct i2c_client *client,
		unsigned int val)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;
	int atime_index=0;

	pr_debug("%s: val=%d\n", __func__, val);

#if 0
	/* minimum 5ms */
	if (val < 3000)
		val = 3000;
#else
	/* Max 15Hz(UI) limitation */
	if (val < 66000)
		val = 66000;
#endif

	/* convert us => ms */
	data->als_poll_delay = val / 1000;

	if (data->als_poll_delay >= 100)
		atime_index = APDS993X_ALS_RES_37888;
	else if (data->als_poll_delay >= 50)
		atime_index = APDS993X_ALS_RES_19456;
	else
		atime_index = APDS993X_ALS_RES_10240;

	ret = apds993x_set_atime(client, apds993x_als_atime_tb[atime_index]);
	if (ret >= 0) {
		data->als_atime_index = atime_index;
		pr_debug("poll delay %d, atime_index %d\n",
				data->als_poll_delay, data->als_atime_index);
	} else {
		return ret;
	}

	/*
	 * If work is already scheduled then subsequent schedules will not
	 * change the scheduled time that's why we have to cancel it first.
	 */
	cancel_delayed_work(&data->als_dwork);
	flush_delayed_work(&data->als_dwork);
	queue_delayed_work(apds993x_workqueue,
			&data->als_dwork,
			msecs_to_jiffies(data->als_poll_delay));

	return 0;
}
#endif

static int apds993x_enable_ps_sensor(struct i2c_client *client, int val)
{
	struct apds993x_data *data = i2c_get_clientdata(client);

	pr_info("%s: val=%d\n", __func__, val);

	if ((val != 0) && (val != 1)) {
		pr_err("%s: invalid value=%d\n", __func__, val);
		return -EINVAL;
	}

	mutex_lock(&data->op_lock);

	if (val == 1) {
		/* turn on p sensor */
		if (data->enable_ps_sensor==0) {
			data->enable_ps_sensor= 1;

			/* Power Off */
			if (apds993x_set_enable(client,0) <0) {
#ifdef CONFIG_APDS9930_DEBUG
			pr_err("%s: apds993x_set_enable 0x00\n", __func__);
#endif
				goto out;
			}

#if 1
			apds993x_set_pilt(client, 1023);    // to force first Near-to-Far interrupt //29-Feb-2012 KK
#ifdef CONFIG_APDS9930_DEBUG
			pr_info("%s: apds993x_set_pilt 1023\n", __func__);
#endif
			apds993x_set_piht(client, 0);
#ifdef CONFIG_APDS9930_DEBUG
			pr_info("%s: apds993x_set_piht 0\n", __func__);
#endif
#else
			/* init threshold for proximity */
			apds993x_set_pilt(client, 0);
			apds993x_set_piht(client,
					apds993x_ps_detection_threshold);
#endif
			/*calirbation*/
			apds993x_set_ps_threshold_adding_cross_talk(client, data->cross_talk);
#if 1
			data->ps_detection = 1;            // we are forcing Near-to-Far interrupt, so this is defaulted to 1
			input_report_abs(data->input_dev_ps, ABS_DISTANCE, 3);/* to send far event to hal */
			input_sync(data->input_dev_ps);
#endif
			/* clear interrupt pin of proximity to keep gpio value high before setting enable register*/
			if (apds993x_set_command(client, 0) <0) {
#ifdef CONFIG_APDS9930_DEBUG
			pr_err("%s: apds993x_set_command 0\n", __func__);
#endif
				goto out;
			}

			if (data->enable_als_sensor==0) {
				/* only enable PS interrupt */
				if (apds993x_set_enable(client, 0x27) <0) {
#ifdef CONFIG_APDS9930_DEBUG
				pr_err("%s: apds993x_set_enable 0x27\n", __func__);
#endif
					goto out;
				}
			} else {
#ifdef ALS_POLLING_ENABLED
				/* enable PS interrupt */
				if (apds993x_set_enable(client, 0x27) <0) {
#ifdef CONFIG_APDS9930_DEBUG
				pr_err("%s: apds993x_set_enable 0x27\n", __func__);
#endif
					goto out;
				}
#else
				/* enable ALS and PS interrupt */
				if (apds993x_set_enable(client, 0x37) <0) {
#ifdef CONFIG_APDS9930_DEBUG
				pr_err("%s: apds993x_set_enable 0x37\n", __func__);
#endif
					goto out;
				}
#endif
			}
			if (apds993x_set_pers(client, 0x02)<0) {
#ifdef CONFIG_APDS9930_DEBUG
			pr_err("%s: apds993x_set_pers 0x02\n", __func__);
#endif
				goto out;
			}
		}
	} else {
		/*
		 * turn off p sensor - kk 25 Apr 2011
		 * we can't turn off the entire sensor,
		 * the light sensor may be needed by HAL
		 */
		data->enable_ps_sensor = 0;
		if (data->enable_als_sensor == 1) {
#ifdef ALS_POLLING_ENABLED
			/* no ALS interrupt */
			if (apds993x_set_enable(client, 0x03) <0) {
#ifdef CONFIG_APDS9930_DEBUG
			pr_err("%s: apds993x_set_enable 0x03\n", __func__);
#endif
				goto out;
			}
			/*
			 * If work is already scheduled then subsequent
			 * schedules will not change the scheduled time
			 * that's why we have to cancel it first.
			 */
			if(!cancel_delayed_work(&data->als_dwork)) {
			    flush_delayed_work(&data->als_dwork);
                        }
			/* 100ms */
			queue_delayed_work(apds993x_workqueue,
					&data->als_dwork,
					msecs_to_jiffies(data->als_poll_delay));
#else
			/* reconfigute light sensor setting */
			/* Power Off */
			if (apds993x_set_enable(client,0) <0) {
#ifdef CONFIG_APDS9930_DEBUG
			pr_err("%s: apds993x_set_enable 0\n", __func__);
#endif
				goto out;
			}
			/* Force ALS interrupt */
			apds993x_set_ailt( client, 0xFFFF);
#ifdef CONFIG_APDS9930_DEBUG
			pr_info("%s: apds993x_set_ailt 0xFFFF\n", __func__);
#endif
			apds993x_set_aiht( client, 0);
#ifdef CONFIG_APDS9930_DEBUG
			pr_info("%s: apds993x_set_aiht 0\n", __func__);
#endif

			// 29-Feb-2012 KK
			// Force PS interrupt every PS conversion cycle
			// instead of comparing threshold value

			if (apds993x_set_enable(client, 0x13) <0) {  /* only enable light sensor and WAIT */
#ifdef CONFIG_APDS9930_DEBUG
			pr_err("%s: apds993x_set_enable 0x13\n", __func__);
#endif
				goto out;
			}
#endif
		} else {
			if (apds993x_set_enable(client, 0) <0) {
#ifdef CONFIG_APDS9930_DEBUG
			pr_err("%s: apds993x_set_enable 0\n", __func__);
#endif
				goto out;
			}
#ifdef ALS_POLLING_ENABLED
			/*
			 * If work is already scheduled then subsequent
			 * schedules will not change the scheduled time
			 * that's why we have to cancel it first.
			 */
			if(!cancel_delayed_work(&data->als_dwork)) {
			    flush_delayed_work(&data->als_dwork);
                        }
#endif
		}
	}
	mutex_unlock(&data->op_lock);
	pr_info("%s: apds993x_enable_ps_sensor_end\n", __func__);
	return 0;

out:
	mutex_unlock(&data->op_lock);
        printk("%s: als_set_enable... io error\n", __func__);
	return -1;
}

static int apds993x_ps_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int apds993x_ps_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long apds993x_ps_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	struct apds993x_data *data;
	struct i2c_client *client;
	int enable;
	int ret = -1;

	if (arg == 0)
		return -EINVAL;

	if (apds993x_i2c_client == NULL) {
		pr_err("%s: i2c driver not installed\n", __func__);
		return -ENODEV;
	}

	client = apds993x_i2c_client;
	data = i2c_get_clientdata(apds993x_i2c_client);

	switch (cmd) {
		case APDS993X_IOCTL_PS_ENABLE:
			ret = copy_from_user(&enable,
					(void __user *)arg, sizeof(enable));
			if (ret) {
				pr_err("%s: PS_ENABLE: copy_from_user failed\n",
						__func__);
				return -EFAULT;
			}

			ret = apds993x_enable_ps_sensor(client, enable);
			if (ret < 0)
				return ret;
			break;

		case APDS993X_IOCTL_PS_GET_ENABLE:
			ret = copy_to_user((void __user *)arg,
					&data->enable_ps_sensor,
					sizeof(data->enable_ps_sensor));
			if (ret) {
				pr_err("%s: PS_GET_ENABLE: copy_to_user failed\n",
						__func__);
				return -EFAULT;
			}

			break;

		case APDS993X_IOCTL_PS_GET_PDATA:
			data->ps_data =	i2c_smbus_read_word_data(client,
					CMD_WORD|APDS993X_PDATAL_REG);

			ret = copy_to_user((void __user *)arg,
					&data->ps_data, sizeof(data->ps_data));
			if (ret) {
				pr_err("%s: PS_GET_PDATA: copy_to_user failed\n",
						__func__);
				return -EFAULT;
			}
			break;

		default:
			pr_warn("%s: unknown ioctl (%d)\n", __func__, cmd);
			break;
	}

	return 0;
}

static int apds993x_als_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int apds993x_als_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long apds993x_als_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	struct apds993x_data *data;
	struct i2c_client *client;
	int enable;
	int ret = -1;

#ifdef ALS_POLLING_ENABLED
	unsigned int delay;
#endif

	if (arg == 0)
		return -EINVAL;

	if (apds993x_i2c_client == NULL) {
		pr_err("%s: i2c driver not installed\n", __func__);
		return -ENODEV;
	}

	client = apds993x_i2c_client;
	data = i2c_get_clientdata(apds993x_i2c_client);

	switch (cmd) {
		case APDS993X_IOCTL_ALS_ENABLE:
			ret = copy_from_user(&enable,
					(void __user *)arg, sizeof(enable));
			if (ret) {
				pr_err("%s: ALS_ENABLE: copy_from_user failed\n",
						__func__);
				return -EFAULT;
			}

			ret = apds993x_enable_als_sensor(client, enable);
			if (ret < 0)
				return ret;
			break;

#ifdef ALS_POLLING_ENABLED
		case APDS993X_IOCTL_ALS_DELAY:
			ret = copy_from_user(&delay, (void __user *)arg, sizeof(delay));
			if (ret) {
				pr_err("%s: ALS_DELAY: copy_to_user failed\n",
						__func__);
				return -EFAULT;
			}

			ret = apds993x_set_als_poll_delay (client, delay);
			if (ret < 0)
				return ret;
			break;
#endif

		case APDS993X_IOCTL_ALS_GET_ENABLE:
			ret = copy_to_user((void __user *)arg,
					&data->enable_als_sensor,
					sizeof(data->enable_als_sensor));
			if (ret) {
				pr_err("%s: ALS_GET_ENABLE: copy_to_user failed\n",
						__func__);
				return -EFAULT;
			}
			break;

		case APDS993X_IOCTL_ALS_GET_CH0DATA:
			data->als_data = i2c_smbus_read_word_data(client,
					CMD_WORD|APDS993X_CH0DATAL_REG);

			ret = copy_to_user((void __user *)arg,
					&data->als_data, sizeof(data->als_data));
			if (ret) {
				pr_err("%s: ALS_GET_CH0DATA: copy_to_user failed\n",
						__func__);
				return -EFAULT;
			}
			break;

		case APDS993X_IOCTL_ALS_GET_CH1DATA:
			data->als_data = i2c_smbus_read_word_data(client,
					CMD_WORD|APDS993X_CH1DATAL_REG);

			ret = copy_to_user((void __user *)arg,
					&data->als_data, sizeof(data->als_data));
			if (ret) {
				pr_err("%s: ALS_GET_CH1DATA: copy_to_user failed\n",
						__func__);
				return -EFAULT;
			}
			break;

		default:
			pr_warn("%s: unknown ioctl (%d)\n", __func__, cmd);
			break;
	}

	return 0;
}

/*
 * SysFS support
 */
static ssize_t apds993x_show_ch0data(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ch0data;

	mutex_lock(&data->update_lock);
	ch0data = i2c_smbus_read_word_data(client,
			CMD_WORD|APDS993X_CH0DATAL_REG);
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%d\n", ch0data);
}

static DEVICE_ATTR(ch0data, S_IRUGO, apds993x_show_ch0data, NULL);

static ssize_t apds993x_show_ch1data(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ch1data;

	mutex_lock(&data->update_lock);
	ch1data = i2c_smbus_read_word_data(client,
			CMD_WORD|APDS993X_CH1DATAL_REG);
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%d\n", ch1data);
}

static DEVICE_ATTR(ch1data, S_IRUGO, apds993x_show_ch1data, NULL);

//                                 
static ssize_t apds993x_show_luxValue(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ch0data, ch1data, pdata;
	int luxValue;

	mutex_lock(&data->update_lock);
	ch0data = i2c_smbus_read_word_data(client,
			CMD_WORD|APDS993X_CH0DATAL_REG);
	ch1data = i2c_smbus_read_word_data(client,
			CMD_WORD|APDS993X_CH1DATAL_REG);
	pdata = i2c_smbus_read_word_data(client,
			CMD_WORD|APDS993X_PDATAL_REG);

	luxValue = LuxCalculation(client, ch0data, ch1data);
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%d\n", luxValue);
}

static DEVICE_ATTR(luxValue, S_IRUGO, apds993x_show_luxValue, NULL);
//                                 

static ssize_t apds993x_show_pdata(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);
	int pdata;

	mutex_lock(&data->update_lock);
	pdata = i2c_smbus_read_word_data(client, CMD_WORD|APDS993X_PDATAL_REG);
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%d\n", pdata);
}

static DEVICE_ATTR(pdata, S_IRUGO, apds993x_show_pdata, NULL);

/*calibration sysfs*/
static ssize_t apds993x_show_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);
	int status;
	int rdata;

	mutex_lock(&data->update_lock);
	status = i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_STATUS_REG);
	rdata = i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_ENABLE_REG);
	mutex_unlock(&data->update_lock);

	pr_info("%s: APDS993x_ENABLE_REG=%2d APDS993x_STATUS_REG=%2d\n",
			__func__, rdata, status);

	return sprintf(buf, "%d\n", status);
}

static DEVICE_ATTR(status, S_IRUSR | S_IRGRP, apds993x_show_status, NULL);

static ssize_t apds993x_show_run_calibration(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", data->cross_talk);
}

static ssize_t apds993x_store_run_calibration(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret = 0;

	/* start calibration */
	ret = apds993x_run_cross_talk_calibration(client);

	/* set threshold for near/far status */
	data->ps_threshold = data->cross_talk + ADD_TO_CROSS_TALK;
	data->ps_hysteresis_threshold =
		data->ps_threshold - SUB_FROM_PS_THRESHOLD;

	pr_info("%s: [piht][pilt][c_t] = [%d][%d][%d]\n", __func__,
			data->ps_threshold,
			data->ps_hysteresis_threshold,
			data->cross_talk);

	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(run_calibration,  S_IWUGO | S_IRUGO,
		apds993x_show_run_calibration,
		apds993x_store_run_calibration);

static ssize_t apds993x_show_ps_default_crosstalk(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", DEFAULT_CROSS_TALK);
}

static ssize_t apds993x_store_ps_default_crosstalk(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);

	data->ps_threshold = DEFAULT_CROSS_TALK + ADD_TO_CROSS_TALK;
	data->ps_hysteresis_threshold =
		data->ps_threshold - SUB_FROM_PS_THRESHOLD;

	pr_info("%s: [piht][pilt][c_t] = [%d][%d][%d]\n", __func__,
			data->ps_threshold,
			data->ps_hysteresis_threshold,
			data->cross_talk);

	return count;
}

static DEVICE_ATTR(ps_default_crosstalk, S_IRUGO | S_IWUSR | S_IWGRP,
		apds993x_show_ps_default_crosstalk,
		apds993x_store_ps_default_crosstalk);

/* for Calibration result */
static ssize_t apds993x_show_ps_cal_result(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", data->ps_cal_result);
}

static DEVICE_ATTR(ps_cal_result, S_IRUGO, apds993x_show_ps_cal_result, NULL);
/*calibration sysfs end*/

#ifdef APDS993X_HAL_USE_SYS_ENABLE
static ssize_t apds993x_show_enable_ps_sensor(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", data->enable_ps_sensor);
}

static ssize_t apds993x_store_enable_ps_sensor(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	pr_debug("%s: val=%ld\n", __func__, val);

	if (val != 0 && val != 1) {
		pr_err("%s: invalid value(%ld)\n", __func__, val);
		return -EINVAL;
	}

	apds993x_enable_ps_sensor(client, val);

	return count;
}

static DEVICE_ATTR(enable_ps_sensor, S_IWUGO | S_IRUGO,
		apds993x_show_enable_ps_sensor,
		apds993x_store_enable_ps_sensor);

static ssize_t apds993x_show_enable_als_sensor(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", data->enable_als_sensor);
}

static ssize_t apds993x_store_enable_als_sensor(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	pr_debug("%s: val=%ld\n", __func__, val);

	if (val != 0 && val != 1) {
		pr_err("%s: invalid value(%ld)\n", __func__, val);
		return -EINVAL;
	}

	apds993x_enable_als_sensor(client, val);

	return count;
}

static DEVICE_ATTR(enable_als_sensor, S_IWUGO | S_IRUGO,
		apds993x_show_enable_als_sensor,
		apds993x_store_enable_als_sensor);

static ssize_t apds993x_show_als_poll_delay(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);

	/* return in micro-second */
	return sprintf(buf, "%d\n", data->als_poll_delay * 1000);
}

static ssize_t apds993x_store_als_poll_delay(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef ALS_POLLING_ENABLED
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	apds993x_set_als_poll_delay(client, val);
#endif

	return count;
}

static DEVICE_ATTR(als_poll_delay, S_IWUSR | S_IWGRP | S_IRUGO,
		apds993x_show_als_poll_delay, apds993x_store_als_poll_delay);

#endif

static ssize_t apds993x_show_adjust_crosstalk(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	/* Show proximity calibration result : 0:Fail, 1:Pass */
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", data->ps_cal_result);
}

static ssize_t apds993x_store_adjust_crosstalk(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	/* Store Proximity Calibration Data */
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	apds993x_set_ps_threshold_adding_cross_talk(client, val);

	return count;
}

static DEVICE_ATTR(adjust_crosstalk, S_IWUGO | S_IRUGO,
		apds993x_show_adjust_crosstalk, apds993x_store_adjust_crosstalk);


#if APDS900_SENSOR_DEBUG
static ssize_t apds993x_show_reg_dump(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	int result;

	result = apds993x_get_register_dump(client);

	if(result){
		pr_err("%s: reg_dump fail\n", __func__);
	}

	return 0;
}

static DEVICE_ATTR(reg_dump, S_IRUGO, apds993x_show_reg_dump, NULL);
#endif

static struct attribute *apds993x_attributes[] = {
	&dev_attr_ch0data.attr,
	&dev_attr_ch1data.attr,
	&dev_attr_luxValue.attr,	//                               
	&dev_attr_pdata.attr,
#ifdef APDS993X_HAL_USE_SYS_ENABLE
	&dev_attr_enable_ps_sensor.attr,
	&dev_attr_enable_als_sensor.attr,
	&dev_attr_als_poll_delay.attr,
#endif
	/*calibration*/
	&dev_attr_status.attr,
	&dev_attr_run_calibration.attr,
	&dev_attr_ps_default_crosstalk.attr,
	&dev_attr_ps_cal_result.attr,
	&dev_attr_adjust_crosstalk.attr,
#if APDS900_SENSOR_DEBUG
	&dev_attr_reg_dump.attr,
#endif
	NULL
};

static const struct attribute_group apds993x_attr_group = {
	.attrs = apds993x_attributes,
};

static struct file_operations apds993x_ps_fops = {
	.owner = THIS_MODULE,
	.open = apds993x_ps_open,
	.release = apds993x_ps_release,
	.unlocked_ioctl = apds993x_ps_ioctl,
};

static struct miscdevice apds993x_ps_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "apds993x_ps_dev",
	.fops = &apds993x_ps_fops,
};

static struct file_operations apds993x_als_fops = {
	.owner = THIS_MODULE,
	.open = apds993x_als_open,
	.release = apds993x_als_release,
	.unlocked_ioctl = apds993x_als_ioctl,
};

static struct miscdevice apds993x_als_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "apds993x_als_dev",
	.fops = &apds993x_als_fops,
};

/*
 * Initialization function
 */

static int apds993x_init_client(struct i2c_client *client)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int err;
	int id;

	err = apds993x_set_enable(client, 0);
	if (err < 0)
		return err;

	id = i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_ID_REG);
	if (id == 0x30) {
		pr_info("%s: APDS9931\n", __func__);
	} else if (id == 0x39) { // APDS9900 --> 0x29
		pr_err("%s: APDS9930\n", __func__);
	} else {
		pr_info("%s: Neither APDS9931 nor APDS9930\n", __func__);
		return -ENODEV;
	}

	/* 100.64ms ALS integration time */
	err = apds993x_set_atime(client,
			apds993x_als_atime_tb[data->als_atime_index]);
	if (err < 0)
		return err;

	/* 2.72ms Prox integration time */
	err = apds993x_set_ptime(client, 0xFF);
	if (err < 0)
		return err;

	/* 2.72ms Wait time */
	err = apds993x_set_wtime(client, 0xFF);
	if (err < 0)
		return err;

	err = apds993x_set_ppcount(client, apds993x_ps_pulse_number);
	if (err < 0)
		return err;

	/* no long wait */
	err = apds993x_set_config(client, 0);
	if (err < 0)
		return err;

	err = apds993x_set_control(client,
			APDS993X_PDRVIE_100MA |
			APDS993X_PRX_IR_DIOD |
			apds993x_ps_pgain |
			apds993x_als_again_bit_tb[data->als_again_index]);
	if (err < 0)
		return err;

	/* init threshold for proximity */
	err = apds993x_set_pilt(client, 0);
	if (err < 0)
		return err;

	err = apds993x_set_piht(client, apds993x_ps_detection_threshold);
	if (err < 0)
		return err;

	apds993x_set_pilt(client, 1023);    // to force first Near-to-Far interrupt // 29-Feb-2012 KK
	apds993x_set_piht(client, 0);
	/*calirbation*/
	apds993x_set_ps_threshold_adding_cross_talk(client, data->cross_talk);
#if 1
	data->ps_detection = 1;            // we are forcing Near-to-Far interrupt, so this is defaulted to 1
#else
	data->ps_detection = 0; /* initial value = far*/
#endif
	/* force first ALS interrupt to get the environment reading */
	err = apds993x_set_ailt(client, 0xFFFF);
	if (err < 0)
		return err;

	err = apds993x_set_aiht(client, 0);
	if (err < 0)
		return err;

	/* 2 consecutive Interrupt persistence */
	err = apds993x_set_pers(client, APDS993X_PPERS_2|APDS993X_APERS_2);
	if (err < 0)
		return err;

	/* sensor is in disabled mode but all the configurations are preset */
	return 0;
}

static int apds993x_suspend(struct device *dev)
{
	struct apds993x_data *data;
	struct apds993x_platform_data *pdata;

	pr_info("%s: apds993x_suspend\n", __func__);

	data = dev_get_drvdata(dev);
	pdata = data->platform_data;

#ifdef APDS993X_PM_IRQ_SYNC
	atomic_set(&data->status, APDS993X_STATUS_SUSPEND);
	if(data->enable_als_sensor) {
		if(!cancel_delayed_work(&data->als_dwork)) {
			flush_delayed_work(&data->als_dwork);
		}
	}
#endif

#if 1 /* Don't need regulator setting in odin */
	if (pdata->power_on)
		pdata->power_on(false);
#endif

	return 0;
}
/*only for REV.0 */
void (*apds9930_lux_change_cb)(int);

/* only one callback is maintained. may change it to list of callbacks */
void apds9930_register_lux_change_callback (void (*callback) (int))
{
	apds9930_lux_change_cb = callback;
}
/*only for REV.0 */

static int apds993x_resume(struct device *dev)
{
	struct apds993x_data *data;
	struct apds993x_platform_data *pdata;

	data = dev_get_drvdata(dev);
	pdata = data->platform_data;

#if 1 /* Don't need regulator setting in odin */
	if (pdata->power_on)
		pdata->power_on(true);
#endif

#ifdef APDS993X_PM_IRQ_SYNC
	if (data->enable_ps_sensor) {
		if (atomic_read(&data->status) == APDS993X_STATUS_QUEUE_WORK) {
			apds993x_reschedule_work(data, 0);
		}
	}
	atomic_set(&data->status, APDS993X_STATUS_RESUME);
	if(data->enable_als_sensor) {
		queue_delayed_work(apds993x_workqueue,
				&data->als_dwork,
				msecs_to_jiffies(data->als_poll_delay));
	}
#endif

	return 0;
}

static int reg_set_optimum_mode_check(struct regulator *reg, int load_uA)
{
	return (regulator_count_voltages(reg) > 0) ?
		regulator_set_optimum_mode(reg, load_uA) : 0;
}

static int sensor_regulator_configure(struct apds993x_data *data, bool on)
{
	struct i2c_client *client = data->client;
	struct apds993x_platform_data *pdata = data->platform_data;
	int rc;

	if (on == false)
		goto hw_shutdown;

	pdata->vcc_ana = regulator_get(&client->dev, "avago,vdd_ana");
	if (IS_ERR(pdata->vcc_ana)) {
		rc = PTR_ERR(pdata->vcc_ana);
		dev_err(&client->dev,
				"Regulator get failed vcc_ana rc=%d\n", rc);
		return rc;
	}

	if (regulator_count_voltages(pdata->vcc_ana) > 0) {
		rc = regulator_set_voltage(pdata->vcc_ana, AVDD_VTG_MIN_UV,
				AVDD_VTG_MAX_UV);
		if (rc) {
			dev_err(&client->dev,
					"regulator set_vtg failed rc=%d\n", rc);
			goto error_set_vtg_vcc_ana;
		}
	}

	if (pdata->digital_pwr_regulator) {
		pdata->vcc_dig = regulator_get(&client->dev, "avago,vddio_dig");
		if (IS_ERR(pdata->vcc_dig)) {
			rc = PTR_ERR(pdata->vcc_dig);
			dev_err(&client->dev,
					"Regulator get dig failed rc=%d\n", rc);
			goto error_get_vtg_vcc_dig;
		}

		if (regulator_count_voltages(pdata->vcc_dig) > 0) {
			rc = regulator_set_voltage(pdata->vcc_dig,
					VDDIO_VTG_DIG_MIN_UV, VDDIO_VTG_DIG_MAX_UV);
			if (rc) {
				dev_err(&client->dev,
						"regulator set_vtg failed rc=%d\n", rc);
				goto error_set_vtg_vcc_dig;
			}
		}
	}

	if (pdata->i2c_pull_up) {
		pdata->vcc_i2c = regulator_get(&client->dev, "avago,vddio_i2c");
		if (IS_ERR(pdata->vcc_i2c)) {
			rc = PTR_ERR(pdata->vcc_i2c);
			dev_err(&client->dev,
					"Regulator get failed rc=%d\n", rc);
			goto error_get_vtg_i2c;
		}
		if (regulator_count_voltages(pdata->vcc_i2c) > 0) {
			rc = regulator_set_voltage(pdata->vcc_i2c,
					VDDIO_I2C_VTG_MIN_UV,
					VDDIO_I2C_VTG_MAX_UV);
			if (rc) {
				dev_err(&client->dev,
						"regulator set_vtg failed rc=%d\n", rc);
				goto error_set_vtg_i2c;
			}
		}
	}

	return 0;

error_set_vtg_i2c:
	regulator_put(pdata->vcc_i2c);
error_get_vtg_i2c:
	if (pdata->digital_pwr_regulator)
		if (regulator_count_voltages(pdata->vcc_dig) > 0)
			regulator_set_voltage(pdata->vcc_dig, 0,
					VDDIO_VTG_DIG_MAX_UV);
error_set_vtg_vcc_dig:
	if (pdata->digital_pwr_regulator)
		regulator_put(pdata->vcc_dig);
error_get_vtg_vcc_dig:
	if (regulator_count_voltages(pdata->vcc_ana) > 0)
		regulator_set_voltage(pdata->vcc_ana, 0, AVDD_VTG_MAX_UV);
error_set_vtg_vcc_ana:
	regulator_put(pdata->vcc_ana);
	return rc;

hw_shutdown:
	if (regulator_count_voltages(pdata->vcc_ana) > 0)
		regulator_set_voltage(pdata->vcc_ana, 0, AVDD_VTG_MAX_UV);
	regulator_put(pdata->vcc_ana);
	if (pdata->digital_pwr_regulator) {
		if (regulator_count_voltages(pdata->vcc_dig) > 0)
			regulator_set_voltage(pdata->vcc_dig, 0,
					VDDIO_VTG_DIG_MAX_UV);
		regulator_put(pdata->vcc_dig);
	}
	if (pdata->i2c_pull_up) {
		if (regulator_count_voltages(pdata->vcc_i2c) > 0)
			regulator_set_voltage(pdata->vcc_i2c, 0,
					VDDIO_I2C_VTG_MAX_UV);
		regulator_put(pdata->vcc_i2c);
	}
	return 0;
}


static int sensor_regulator_power_on(struct apds993x_data *data, bool on)
{
	struct i2c_client *client = data->client;
	struct apds993x_platform_data *pdata = data->platform_data;
	int rc;

	if (on == false)
		goto power_off;

	rc = reg_set_optimum_mode_check(pdata->vcc_ana, AVDD_ACTIVE_LOAD_UA);
	if (rc < 0) {
		dev_err(&client->dev,
				"Regulator vcc_ana set_opt failed rc=%d\n", rc);
		return rc;
	}

	rc = regulator_enable(pdata->vcc_ana);
	if (rc) {
		dev_err(&client->dev,
				"Regulator vcc_ana enable failed rc=%d\n", rc);
		goto error_reg_en_vcc_ana;
	}

	if (pdata->digital_pwr_regulator) {
		rc = reg_set_optimum_mode_check(pdata->vcc_dig,
				VDDIO_ACTIVE_LOAD_DIG_UA);
		if (rc < 0) {
			dev_err(&client->dev,
					"Regulator vcc_dig set_opt failed rc=%d\n",
					rc);
			goto error_reg_opt_vcc_dig;
		}

		rc = regulator_enable(pdata->vcc_dig);
		if (rc) {
			dev_err(&client->dev,
					"Regulator vcc_dig enable failed rc=%d\n", rc);
			goto error_reg_en_vcc_dig;
		}
	}

	if (pdata->i2c_pull_up) {
		rc = reg_set_optimum_mode_check(pdata->vcc_i2c,
				VDDIO_I2C_LOAD_UA);
		if (rc < 0) {
			dev_err(&client->dev,
					"Regulator vcc_i2c set_opt failed rc=%d\n", rc);
			goto error_reg_opt_i2c;
		}

		rc = regulator_enable(pdata->vcc_i2c);
		if (rc) {
			dev_err(&client->dev,
					"Regulator vcc_i2c enable failed rc=%d\n", rc);
			goto error_reg_en_vcc_i2c;
		}
	}

	msleep(130);

	return 0;

error_reg_en_vcc_i2c:
	if (pdata->i2c_pull_up)
		reg_set_optimum_mode_check(pdata->vcc_i2c, 0);
error_reg_opt_i2c:
	if (pdata->digital_pwr_regulator)
		regulator_disable(pdata->vcc_dig);
error_reg_en_vcc_dig:
	if (pdata->digital_pwr_regulator)
		reg_set_optimum_mode_check(pdata->vcc_dig, 0);
error_reg_opt_vcc_dig:
	regulator_disable(pdata->vcc_ana);
error_reg_en_vcc_ana:
	reg_set_optimum_mode_check(pdata->vcc_ana, 0);
	return rc;

power_off:
	reg_set_optimum_mode_check(pdata->vcc_ana, 0);
	regulator_disable(pdata->vcc_ana);
	if (pdata->digital_pwr_regulator) {
		reg_set_optimum_mode_check(pdata->vcc_dig, 0);
		regulator_disable(pdata->vcc_dig);
	}
	if (pdata->i2c_pull_up) {
		reg_set_optimum_mode_check(pdata->vcc_i2c, 0);
		regulator_disable(pdata->vcc_i2c);
	}
	msleep(50);
	return 0;
}

static int sensor_platform_hw_power_on(bool on)
{
	if (pdev_data == NULL)
		return -ENODEV;

#if 1 /* Don't need regulator setting in odin */
	sensor_regulator_power_on(pdev_data, on);
#endif

	return 0;
}

static int sensor_platform_hw_init(void)
{
	struct i2c_client *client;
	struct apds993x_data *data;
	int error;

	if (pdev_data == NULL)
		return -ENODEV;

	data = pdev_data;
	client = data->client;

#if 1 /* Don't need regulator setting in odin */
	error = sensor_regulator_configure(data, true);
	if (error < 0) {
		dev_err(&client->dev, "unable to configure regulator\n");
		return error;
	}
#endif

	if (gpio_is_valid(data->platform_data->irq_gpio)) {
		/* configure touchscreen irq gpio */
		error = gpio_request_one(data->platform_data->irq_gpio,
				GPIOF_DIR_IN,
				"apds993x_irq_gpio");
		if (error) {
			dev_err(&client->dev, "unable to request gpio %d\n",
					data->platform_data->irq_gpio);
		}
		data->irq = client->irq =
			gpio_to_irq(data->platform_data->irq_gpio);
	} else {
		dev_err(&client->dev, "irq gpio not provided\n");
	}
	return 0;
}

static void sensor_platform_hw_exit(void)
{
	struct apds993x_data *data = pdev_data;

	if (data == NULL)
		return;

#if 1 /* Don't need regulator setting in odin */
	sensor_regulator_configure(data, false);
#endif

	if (gpio_is_valid(data->platform_data->irq_gpio))
		gpio_free(data->platform_data->irq_gpio);
}

static int sensor_parse_dt(struct device *dev,
		struct apds993x_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
	unsigned int tmp;
	int rc;

	/* regulator info */
	pdata->i2c_pull_up = of_property_read_bool(np, "avago,i2c-pull-up");
	pdata->digital_pwr_regulator = NULL;

	/* reset, irq gpio info 
	rc = of_property_read_u32(np, "avago,irq-gpio", &tmp);
	if (rc) {
		dev_err(dev, "Unable to read irq-gpio\n");
		return rc;
	}
	pdata->irq_gpio = tmp;*/
	if ((rc= of_get_named_gpio(np, "avago,irq-gpio", 0)) >= 0) {
		pdata->irq_gpio = rc;
	}
	else
	{
		printk(KERN_INFO "Fail read irq-gpio ret: %d\n", rc);
		rc = -1;
		return rc;
	}
	pr_info("%s: avago irq-gpio = %d\n", __func__, pdata->irq_gpio);

	/* set functions of platform data */
	pdata->init = sensor_platform_hw_init;
	pdata->exit = sensor_platform_hw_exit;
	pdata->power_on = sensor_platform_hw_power_on;

	/* ps tuning data*/
	rc = of_property_read_u32(np, "avago,ps_threshold", &tmp);
	if (rc) {
		dev_err(dev, "Unable to read ps_threshold\n");
		return rc;
	}
	pdata->prox_threshold = tmp;

	rc = of_property_read_u32(np, "avago,ps_hysteresis_threshold", &tmp);
	if (rc) {
		dev_err(dev, "Unable to read ps_hysteresis_threshold\n");
		return rc;
	}
	pdata->prox_hsyteresis_threshold = tmp;

	rc = of_property_read_u32(np, "avago,ps_pulse", &tmp);
	if (rc) {
		dev_err(dev, "Unable to read ps_pulse\n");
		return rc;
	}
	pdata->prox_pulse = tmp;

	rc = of_property_read_u32(np, "avago,ps_pgain", &tmp);
	if (rc) {
		dev_err(dev, "Unable to read ps_pgain\n");
		return rc;
	}
	pdata->prox_gain = tmp;

	/* ALS tuning value */
	rc = of_property_read_u32(np, "avago,als_B", &tmp);
	if (rc) {
		dev_err(dev, "Unable to read apds993x_coe_b\n");
		return rc;
	}
	pdata->als_B = tmp;

	rc = of_property_read_u32(np, "avago,als_C", &tmp);
	if (rc) {
		dev_err(dev, "Unable to read apds993x_coe_c\n");
		return rc;
	}
	pdata->als_C = tmp;

	rc = of_property_read_u32(np, "avago,als_D", &tmp);
	if (rc) {
		dev_err(dev, "Unable to read apds993x_coe_d\n");
		return rc;
	}
	pdata->als_D = tmp;

	rc = of_property_read_u32(np, "avago,ga_value", &tmp);
	if (rc) {
		dev_err(dev, "Unable to read ga_value\n");
		return rc;
	}
	pdata->ga_value = tmp;

	return 0;
}

/*
 * I2C init/probing/exit functions
 */
static struct i2c_driver apds993x_driver;
static int apds993x_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct apds993x_data *data;
	struct apds993x_platform_data *pdata;
	int err = 0;

	pr_debug("%s\n", __func__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE)) {
		err = -EIO;
		goto exit;
	}

	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
				sizeof(struct apds993x_platform_data),
				GFP_KERNEL);
		if (!pdata) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}

		client->dev.platform_data = pdata;
		err = sensor_parse_dt(&client->dev, pdata);
		if (err) {
			pr_err("%s: sensor_parse_dt() err\n", __func__);
			return err;
		}
	} else {
		pdata = client->dev.platform_data;
		if (!pdata) {
			dev_err(&client->dev, "No platform data\n");
			return -ENODEV;
		}
	}

	/* Set the default parameters */
	apds993x_ps_detection_threshold = pdata->prox_threshold;
	apds993x_ps_hsyteresis_threshold = pdata->prox_hsyteresis_threshold;
	apds993x_ps_pulse_number = pdata->prox_pulse;
	apds993x_ps_pgain = pdata->prox_gain;

	apds993x_coe_b = pdata->als_B;
	apds993x_coe_c = pdata->als_C;
	apds993x_coe_d = pdata->als_D;
	apds993x_ga = pdata->ga_value;

	data = kzalloc(sizeof(struct apds993x_data), GFP_KERNEL);
	if (!data) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		err = -ENOMEM;
		goto exit;
	}
	pdev_data = data;

	data->platform_data = pdata;
	data->client = client;
	apds993x_i2c_client = client;

	/* h/w initialization */
	if (pdata->init)
		err = pdata->init();
		if (err <0) {
			pr_err("%s: Could not allocate APDS993X_INT !\n", __func__);
			kfree(data);
			pdev_data = NULL;
			goto exit;
		}

	if (pdata->power_on)
		err = pdata->power_on(true);
		if (err <0) {
			pr_err("%s: Could not power_on !\n", __func__);
			kfree(data);
			pdev_data = NULL;
			goto exit;
		}

	i2c_set_clientdata(client, data);

	data->enable = 0;	/* default mode is standard */
	data->ps_threshold = apds993x_ps_detection_threshold;
	data->ps_hysteresis_threshold = apds993x_ps_hsyteresis_threshold;
	data->ps_detection = 0;	/* default to no detection */
	data->enable_als_sensor = 0;	// default to 0
	data->enable_ps_sensor = 0;	// default to 0
	data->als_poll_delay = 100;	// default to 100ms
	data->als_atime_index = APDS993X_ALS_RES_37888;	// 100ms ATIME
	data->als_again_index = APDS993X_ALS_GAIN_8X;	// 8x AGAIN
	data->als_reduce = 0;	// no ALS 6x reduction
	data->als_prev_lux = 0;

//	spin_lock_init(&data->lock);

	mutex_init(&data->update_lock);
	mutex_init(&data->op_lock);

	/* calibration */
	if (apds993x_cross_talk_val > 0 && apds993x_cross_talk_val < 1000) {
		data->cross_talk = apds993x_cross_talk_val;
	} else {
		/*
		 * default value: Get the cross-talk value from the memory.
		 * This value is saved during the cross-talk calibration
		 */
		data->cross_talk = DEFAULT_CROSS_TALK;
	}

	err = request_irq(data->irq, apds993x_interrupt, IRQF_DISABLED|IRQ_TYPE_EDGE_FALLING|IRQF_NO_SUSPEND,
			APDS993X_DRV_NAME, (void *)client);
	if (err < 0) {
		pr_err("%s: Could not request_irq!\n", __func__);
		goto exit_kfree;
	}

	err = irq_set_irq_wake(client->irq, 1);
	if (err < 0) {
		pr_err("%s: Could not irq_set_irq_wake!\n", __func__);
		goto exit_free_irq;
	}

#ifdef APDS993X_PM_IRQ_SYNC
	atomic_set(&data->status, APDS993X_STATUS_RESUME);
	wake_lock_init(&data->wakelock, WAKE_LOCK_SUSPEND, "apds993x");
#endif
	INIT_DELAYED_WORK(&data->dwork, apds993x_work_handler);

#ifdef ALS_POLLING_ENABLED
	INIT_DELAYED_WORK(&data->als_dwork, apds993x_als_polling_work_handler);
#endif

	/* Initialize the APDS993X chip */
	err = apds993x_init_client(client);
	if (err) {
		pr_err("%s: Failed to init apds993x\n", __func__);
		goto exit_free_dev_als;
	}

	/* Register to Input Device */
	data->input_dev_als = input_allocate_device();
	if (!data->input_dev_als) {
		err = -ENOMEM;
		pr_err("%s: Failed to allocate input device als\n", __func__);
		goto exit_free_dev_als;
	}

	data->input_dev_ps = input_allocate_device();
	if (!data->input_dev_ps) {
		err = -ENOMEM;
		pr_err("%s: Failed to allocate input device ps\n", __func__);
		goto exit_free_dev_als;
	}

	set_bit(EV_ABS, data->input_dev_als->evbit);
	set_bit(EV_ABS, data->input_dev_ps->evbit);

	input_set_abs_params(data->input_dev_als, ABS_LIGHT, 0, 30001, 0, 0);
	input_set_abs_params(data->input_dev_ps, ABS_DISTANCE, 0, 5, 0, 0);

	data->input_dev_als->name = "Avago light sensor";
	data->input_dev_ps->name = "Avago proximity sensor";
	data->input_dev_ps->uniq = APDS993X_DRV_NAME;
	data->input_dev_ps->dev.init_name = LGE_PROXIMITY_NAME;

	err = input_register_device(data->input_dev_als);
	if (err) {
		err = -ENOMEM;
		pr_err("%s: Unable to register input device als: %s\n",
				__func__, data->input_dev_als->name);
		goto exit_free_dev_ps;
	}

	err = input_register_device(data->input_dev_ps);
	if (err) {
		err = -ENOMEM;
		pr_err("%s: Unable to register input device ps: %s\n",
				__func__, data->input_dev_ps->name);
		goto exit_unregister_dev_als;
	}

	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &apds993x_attr_group);
	if (err)
		goto exit_unregister_dev_ps;

	/* Register for sensor ioctl */
	err = misc_register(&apds993x_ps_device);
	if (err) {
		pr_err("%s: Unable to register ps ioctl: %d", __func__, err);
		goto exit_remove_sysfs_group;
	}

	err = misc_register(&apds993x_als_device);
	if (err) {
		pr_err("%s: Unable to register als ioctl: %d", __func__,  err);
		goto exit_unregister_ps_ioctl;
	}

	pr_info("%s: Support ver. %s enabled\n", __func__, DRIVER_VERSION);

	return 0;

exit_unregister_ps_ioctl:
	misc_deregister(&apds993x_ps_device);
exit_remove_sysfs_group:
	sysfs_remove_group(&client->dev.kobj, &apds993x_attr_group);
exit_unregister_dev_ps:
	input_unregister_device(data->input_dev_ps);
exit_unregister_dev_als:
	input_unregister_device(data->input_dev_als);
exit_free_dev_ps:
exit_free_dev_als:
	irq_set_irq_wake(client->irq, 0);
exit_free_irq:
	free_irq(data->irq, client);
exit_kfree:
	if (pdata->power_on)
		pdata->power_on(false);
	if (pdata->exit)
		pdata->exit();
#ifdef APDS993X_PM_IRQ_SYNC
	wake_lock_destroy(&data->wakelock);
#endif
	kfree(data);
	pdev_data = NULL;
exit:
	return err;
}

static int apds993x_remove(struct i2c_client *client)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	struct apds993x_platform_data *pdata = data->platform_data;

	/* Power down the device */
	apds993x_set_enable(client, 0);

	misc_deregister(&apds993x_als_device);
	misc_deregister(&apds993x_ps_device);

	sysfs_remove_group(&client->dev.kobj, &apds993x_attr_group);

	input_unregister_device(data->input_dev_ps);
	input_unregister_device(data->input_dev_als);

	irq_set_irq_wake(client->irq, 0);
	free_irq(client->irq, data);

	if (pdata->power_on)
		pdata->power_on(false);

	if (pdata->exit)
		pdata->exit();

#ifdef APDS993X_PM_IRQ_SYNC
	wake_lock_destroy(&data->wakelock);
#endif

	kfree(data);
	pdev_data = NULL;

	return 0;
}

static const struct i2c_device_id apds993x_id[] = {
	{ "apds993x", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, apds993x_id);

static struct of_device_id apds993X_match_table[] = {
	{ .compatible = "avago,apds9930",},
	{ },
};

static const struct dev_pm_ops apds993x_pm_ops = {
	.suspend	= apds993x_suspend,
	.resume 	= apds993x_resume,
};

static struct i2c_driver apds993x_driver = {
	.driver = {
		.name   = APDS993X_DRV_NAME,
		.owner  = THIS_MODULE,
		.of_match_table = apds993X_match_table,
		.pm = &apds993x_pm_ops,
	},
	.probe  = apds993x_probe,
	.remove = apds993x_remove,
	.id_table = apds993x_id,
};

static void apds993x_init_async(void *data, async_cookie_t cookie)
{
	int ret = 0;
	apds993x_workqueue = create_workqueue("proximity_als");
	if (!apds993x_workqueue) {
		pr_err("%s: out of memory\n", __func__);
		return;
	}

	ret = i2c_add_driver(&apds993x_driver);

	if(ret != 0) {
		printk("%s: can't add i2c driver\n", __FUNCTION__);
	}
	return;
}

static int __init apds993x_init(void)
{
	async_schedule(apds993x_init_async, NULL);

	return 0;
}
static void __exit apds993x_exit(void)
{
	if (apds993x_workqueue)
		destroy_workqueue(apds993x_workqueue);
	i2c_del_driver(&apds993x_driver);
}

MODULE_AUTHOR("Lee Kai Koon <kai-koon.lee@avagotech.com>");
MODULE_DESCRIPTION("APDS993X ambient light + proximity sensor driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

module_init(apds993x_init);
module_exit(apds993x_exit);
