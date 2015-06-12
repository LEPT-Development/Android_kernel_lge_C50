/* include/linux/lge_touch_core.h
 *
 * Copyright (C) 2012 LGE.
 *
 * Writer: yehan.ahn@lge.com, 	hyesung.shin@lge.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef LGE_TOUCH_SYNAPTICS_H
#define LGE_TOUCH_SYNAPTICS_H

#define NUM_OF_EACH_FINGER_DATA_REG		8
#define MAX_NUM_OF_FINGERS			10
#define MAX_NUM_OF_DEBUG_REASON		2

#define DESCRIPTION_TABLE_START			0xe9
#define EXIST_OFFSET                            0xEE

#define PAGE_SELECT_REG				0xFF		/* Button exists Page 02 */
#define PAGE_MAX_NUM				5		/* number of page register */

#define F12_NO_OBJECT_STATUS		(0x00)
#define F12_FINGER_STATUS			(0x01)
#define F12_STYLUS_STATUS			(0x02)
#define F12_PALM_STATUS				(0x03)
#define F12_HOVERING_FINGER_STATUS	(0x05)
#define F12_GLOVED_FINGER_STATUS	(0x06)

#define OBJECT_FINGER_BIT       0
#define OBJECT_STYLUS_BIT       1
#define OBJECT_PALM_BIT         2
#define OBJECT_UNCLASSIFIED_OBJECT_BIT  3
#define OBJECT_HOVERING_FINGER_BIT  4
#define OBJECT_GLOVEED_FINGER_BIT   5
#define OBJECT_NARROW_OBJECT_SWIPE_BIT  6
#define OBJECT_HAND_EDGE_BUT        7

#define S3621           0
#define S3528_A0        1
#define S3528_A1        2
#define S3528_A1_SUN    3
#define TD4191    4
#define TD4191_TFT_ITO_PANEL    5

#define F35_ERROR_CODE_OFFSET 0
#define F35_CHUNK_NUM_LSB_OFFSET 0
#define F35_CHUNK_NUM_MSB_OFFSET 1
#define F35_CHUNK_DATA_OFFSET 2
#define F35_CHUNK_COMMAND_OFFSET 18

#define F35_CHUNK_SIZE 16
#define F35_ERASE_ALL_WAIT_MS 2000
#define F35_RESET_WAIT_MS 250

struct function_descriptor {
	u8 	query_base;
	u8 	command_base;
	u8 	control_base;
	u8 	data_base;
	u8 	int_source_count;
	u8 	id;
};

struct ts_ic_function {
	struct function_descriptor dsc;
	u8 	function_page;
};

struct synaptics_ts_f12_ctrl_23 {
    union {
        struct {
            unsigned char obj_type_enable;
            unsigned char max_reported_objects;
        };
        unsigned char data[2];
    };
};

struct finger_data {
	u8	finger_reg[MAX_NUM_OF_FINGERS][NUM_OF_EACH_FINGER_DATA_REG];
};

struct button_data {
	u16	key_code;
};

struct cur_touch_data {
	u8	device_status_reg;		/* DEVICE_STATUS_REG */
	u8	interrupt_status_reg;
	u8	button_data_reg;
	struct finger_data	finger;
	struct button_data	button;
};

struct synaptics_ts_fw_info {
	u8		fw_version[5];
	u8		fw_product_id[11];
	u8		fw_image_version[5];
	u8		fw_image_product_id[11];
	unsigned char	*fw_start;
	unsigned char   family;
	unsigned char   fw_revision;
	unsigned long	fw_size;
	u8		need_rewrite_firmware;
};

struct lpwg_control {
	u8		lpwg_mode;
	u8              screen;
	u8              sensor;
	u8              qcover;
	u8		double_tap_enable;
	u8 		password_enable;
	u8		signature_enable;
	u8		lpwg_is_enabled;
	u8		has_debug_module;
	atomic_t	is_suspend;
};

struct lpwg_password_data {
	u8		tap_count;
	u8		data_num;
	u8              double_tap_check;
	struct point 	data[MAX_POINT_SIZE_FOR_LPWG];
};

struct state_flag {
	u8		ts_noise_log_flag;
	u8		check_noise_menu;
};

struct palm_data {
	bool curr_palm_mask[MAX_NUM_OF_FINGERS];
	bool prev_palm_mask[MAX_NUM_OF_FINGERS];
	struct point palm_coordinate[MAX_NUM_OF_FINGERS];
	u8 curr_palm_num;
	u8 prev_palm_num;
	bool all_palm_released;
};

struct swipe_data {
	u8	support_swipe;
	u8	swipe_enable_mask;
	u8	swipe_gesture;
	u8	swipe_min_distance;
	u8	swipe_ratio_threshold;
	u8	swipe_ratio_check_period;
	u8	swipe_ratio_check_min_distance;
	u16	min_swipe_time_threshold;
	u16	max_swipe_time_threshold;
	u8	swipe_enable_reg;
	u8	swipe_min_distance_reg;
	u8	swipe_ratio_threshold_reg;
	u8	swipe_ratio_check_period_reg;
	u8	swipe_ratio_check_min_distance_reg;
	u8	min_swipe_time_thres_lsb_reg;
	u8	min_swipe_time_thres_msb_reg;
	u8	max_swipe_time_thres_lsb_reg;
	u8	max_swipe_time_thres_msb_reg;
	u8	swipe_coordinate_start_reg;
	u8	swipe_coordinate_end_reg;
	u8	swipe_debug_reason_reg;
	u8	swipe_time_reg;
};

struct synaptics_ts_data {
	u8	is_probed;
	u8	is_init;
	u8      object_report;
	struct lpwg_control	lpwg_ctrl;
	struct lpwg_password_data	pw_data;
//	struct regulator	*regulator_vdd;
//	struct regulator	*regulator_vio;
	struct regulator *vdd_regulator[TOUCH_PWR_NUM];
	struct i2c_client	*client;
	struct ts_ic_function	common_fc;
	struct ts_ic_function	finger_fc;
	struct ts_ic_function	button_fc;
	struct ts_ic_function	analog_fc;
	struct ts_ic_function	sensor_fc;
	struct ts_ic_function	flash_fc;
	struct ts_ic_function	video_fc;
	struct ts_ic_function	custom_fc;
	struct cur_touch_data	ts_data;
	struct synaptics_ts_fw_info	fw_info;
	struct delayed_work	work_timer;
	struct delayed_work	diff_node_timer;  //test code
	struct delayed_work     work_palm;
	struct wake_lock	timer_wake_lock;
	struct touch_platform_data	*pdata;
	const struct state_info	*state;
	u8	fw_flag;
	struct state_flag		ts_state_flag;
	unsigned int bad_sample;
	char reason[NUM_OF_EACH_FINGER_DATA_REG];
	int h_err_cnt;
	int v_err_cnt;
	bool ubootloader_mode;
	struct palm_data                ts_palm_data;
	struct swipe_data		ts_swipe_data;
};

struct synaptics_ts_exp_fn {
	int (*init)(struct synaptics_ts_data *ts);
	void (*remove)(struct synaptics_ts_data *ts);
	void (*reset)(struct synaptics_ts_data *ts);
	void (*reinit)(struct synaptics_ts_data *ts);
	void (*early_suspend)(struct synaptics_ts_data *ts);
	void (*suspend)(struct synaptics_ts_data *ts);
	void (*resume)(struct synaptics_ts_data *ts);
	void (*late_resume)(struct synaptics_ts_data *ts);
	void (*attn)(unsigned char intr_status_reg);
};

struct synaptics_ts_f12_query_5 {
	union {
		struct {
			unsigned char size_of_query_6;
			struct {
				unsigned char ctrl_00_is_present:1;
				unsigned char ctrl_01_is_present:1;
				unsigned char ctrl_02_is_present:1;
				unsigned char ctrl_03_is_present:1;
				unsigned char ctrl_04_is_present:1;
				unsigned char ctrl_05_is_present:1;
				unsigned char ctrl_06_is_present:1;
				unsigned char ctrl_07_is_present:1;
			} __packed;
			struct {
				unsigned char ctrl_08_is_present:1;
				unsigned char ctrl_09_is_present:1;
				unsigned char ctrl_10_is_present:1;
				unsigned char ctrl_11_is_present:1;
				unsigned char ctrl_12_is_present:1;
				unsigned char ctrl_13_is_present:1;
				unsigned char ctrl_14_is_present:1;
				unsigned char ctrl_15_is_present:1;
			} __packed;
			struct {
				unsigned char ctrl_16_is_present:1;
				unsigned char ctrl_17_is_present:1;
				unsigned char ctrl_18_is_present:1;
				unsigned char ctrl_19_is_present:1;
				unsigned char ctrl_20_is_present:1;
				unsigned char ctrl_21_is_present:1;
				unsigned char ctrl_22_is_present:1;
				unsigned char ctrl_23_is_present:1;
			} __packed;
			struct {
				unsigned char ctrl_24_is_present:1;
				unsigned char ctrl_25_is_present:1;
				unsigned char ctrl_26_is_present:1;
				unsigned char ctrl_27_is_present:1;
				unsigned char ctrl_28_is_present:1;
				unsigned char ctrl_29_is_present:1;
				unsigned char ctrl_30_is_present:1;
				unsigned char ctrl_31_is_present:1;
			} __packed;
		};
		unsigned char data[5];
	};
};

struct synaptics_ts_f12_query_8 {
	union {
		struct {
			unsigned char size_of_query_9;
			struct {
				unsigned char data_00_is_present:1;
				unsigned char data_01_is_present:1;
				unsigned char data_02_is_present:1;
				unsigned char data_03_is_present:1;
				unsigned char data_04_is_present:1;
				unsigned char data_05_is_present:1;
				unsigned char data_06_is_present:1;
				unsigned char data_07_is_present:1;
			} __packed;
			struct {
				unsigned char data_08_is_present:1;
				unsigned char data_09_is_present:1;
				unsigned char data_10_is_present:1;
				unsigned char data_11_is_present:1;
				unsigned char data_12_is_present:1;
				unsigned char data_13_is_present:1;
				unsigned char data_14_is_present:1;
				unsigned char data_15_is_present:1;
			} __packed;
		};
		unsigned char data[3];
	};
};

enum {
	CHARGERLOGO_MODE = 0,
	NORMAL_BOOT_MODE,
};

enum{
	TS_NOISE_LOG_DISABLE = 0,
	TS_NOISE_LOG_ENABLE,
};

enum{
	MENU_OUT = 0,
	MENU_ENTER,
};

enum {
	THERMAL_LOW = 0,
	THERMAL_HIGH,
};

enum {
	DEBUG_DISTANCE_INTER_TAP = 1,
	DEBUG_DISTANCE_TOUCHSLOP,
	DEBUG_TIMEOUT_INTER_TAP,
	DEBUG_MULTI_FINGER,
	DEBUG_DELAY_TIME,
	DEBUG_PALM_STATE,
	DEBUG_ACTIVE_AREA,
	DEBUG_TAP_COUNT,
};

enum {
	NO_SUPPORT_SWIPE = 0,
	SUPPORT_SWIPE,
};

extern struct workqueue_struct *touch_wq;
extern char f54_wlog_buf[6000];
enum error_type synaptics_ts_init(struct i2c_client *client);
extern void SCAN_PDT(void);
int compare_fw_version(struct i2c_client *client, struct touch_fw_info *fw_info);
//mode:0 => write_log, mode:1 && buf => cat, mode:2 && buf => delta
extern int F54Test(int input, int mode, char *buf);
extern int GetImageReport(char *buf);

void synaptics_ts_prox_function(struct synaptics_ts_exp_fn *prox_fn, bool insert);
void synaptics_ts_rmidev_function(struct synaptics_ts_exp_fn *rmidev_fn, bool insert);

/* extern function */
extern int FirmwareUpgrade(struct synaptics_ts_data *ts, const char* fw_path);
extern int FirmwareRecovery(struct synaptics_ts_data *ts, const char *fw_path);
extern int SynaScanPDT(struct synaptics_ts_data *ts);
int synaptics_ts_page_data_read(struct i2c_client *client, u8 page, u8 reg, int size, u8 *data);
int synaptics_ts_page_data_write(struct i2c_client *client, u8 page, u8 reg, int size, u8 *data);
int synaptics_ts_page_data_write_byte(struct i2c_client *client, u8 page, u8 reg, u8 data);
int print_tci_debug_result(struct synaptics_ts_data *ts, int num);
#endif
