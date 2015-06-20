/*
   +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2012 Synaptics, Inc.

   Permission is hereby granted, free of charge, to any person obtaining a copy of
   this software and associated documentation files (the "Software"), to deal in
   the Software without restriction, including without limitation the rights to use,
   copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
   Software, and to permit persons to whom the Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.


   +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 */
#define SYNA_F34_SAMPLE_CODE
#define SHOW_PROGRESS

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/jiffies.h>

#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/wakelock.h>

#include <linux/input/TD4191/lge_touch_core.h>
#include <linux/input/TD4191/touch_synaptics.h>
#include <linux/firmware.h>
unsigned short SynaF35QueryBase;
unsigned short SynaF35ControlBase;
unsigned short SynaF35DataBase;

/* Variables for F34 functionality */
unsigned short SynaF34DataBase;
unsigned short SynaF34QueryBase;
unsigned short SynaF01DataBase;
unsigned short SynaF01ControlBase;
unsigned short SynaF01CommandBase;
unsigned short SynaF01QueryBase;

unsigned short SynaF34Reflash_BlockNum;
unsigned short SynaF34Reflash_BlockData;
unsigned short SynaF34ReflashQuery_BootID;
unsigned short SynaF34ReflashQuery_FlashPropertyQuery;
unsigned short SynaF34ReflashQuery_BlockSize;
unsigned short SynaF34ReflashQuery_FirmwareBlockCount;
unsigned short SynaF34ReflashQuery_ConfigBlockCount;

unsigned char SynaF01Query43Length;

unsigned short SynaFirmwareBlockSize;
unsigned short SynaFirmwareBlockCount;
unsigned long SynaImageSize;

unsigned short SynaConfigBlockSize;
unsigned short SynaConfigBlockCount;
unsigned long SynaConfigImageSize;

// TD4191
unsigned short SynaDisplayBlockSize;
unsigned short SynaDisplayBlockCount;
unsigned long SynaDisplayConfigImgStartAddr;

unsigned short SynaBootloadID;

unsigned short SynaF34_FlashControl;
unsigned short SynaF34_FlashStatus;

unsigned char *SynafirmwareImgData;
unsigned char *SynaconfigImgData;
unsigned char *SynalockImgData;
unsigned char *SynaDisplayConfigImgData;	// TD4191
unsigned int SynafirmwareImgVersion;

unsigned char *my_image_bin;
unsigned long my_image_size;
u8	fw_image_config_id[5];

unsigned char *ConfigBlock;

void CompleteReflash(struct synaptics_ts_data *ts);
void FlashRecovery(struct synaptics_ts_data *ts);
void SynaInitialize(struct synaptics_ts_data *ts);
void SynaReadFirmwareInfo(struct synaptics_ts_data *ts);
void SynaEnableFlashing(struct synaptics_ts_data *ts);
void SynaProgramFirmware(struct synaptics_ts_data *ts);
void SynaFinalizeReflash(struct synaptics_ts_data *ts);
unsigned int SynaWaitForATTN(int time, struct synaptics_ts_data *ts);
bool CheckTouchControllerType(struct synaptics_ts_data *ts);
/*bool CheckFimrwareRevision(struct synaptics_ts_data *ts);*/
void eraseAllBlock(struct synaptics_ts_data *ts);
void SynaUpdateConfig(struct synaptics_ts_data *ts);
void EraseConfigBlock(struct synaptics_ts_data *ts);

enum FlashCommand {
	m_uF34ReflashCmd_FirmwareCrc        = 0x01,   // prior to V2 bootloaders
	m_uF34ReflashCmd_FirmwareWrite      = 0x02,
	m_uF34ReflashCmd_EraseAll           = 0x03,
	m_uF34ReflashCmd_LockDown           = 0x04,   // V2 and later bootloaders
	m_uF34ReflashCmd_ConfigRead         = 0x05,
	m_uF34ReflashCmd_ConfigWrite        = 0x06,
	m_uF34ReflashCmd_EraseUIConfig      = 0x07,
	m_uF34ReflashCmd_Enable             = 0x0F,
	m_uF34ReflashCmd_QuerySensorID      = 0x08,
	m_uF34ReflashCmd_EraseBLConfig      = 0x09,
	m_uF34ReflashCmd_EraseDisplayConfig = 0x0A,
};

enum F35RecoveryCommand {
	CMD_F35_IDLE = 0x0,
	CMD_F35_RESERVED = 0x1,
	CMD_F35_WRITE_CHUNK = 0x2,
	CMD_F35_ERASE_ALL = 0x3,
	CMD_F35_RESET = 0x10,
};

#if 1 // refresh screen after F35 recovery.
extern void mdss_lcd_lut_update(void);
#endif

char SynaFlashCommandStr[0x0C][0x20] = {
	"",
	"FirmwareCrc",
	"FirmwareWrite",
	"EraseAll",
	"LockDown",
	"ConfigRead",
	"ConfigWrite",
	"EraseUIConfig",
	"Enable",
	"QuerySensorID",
	"EraseBLConfig",
	"EraseDisplayConfig",
};

int FirmwareUpgrade(struct synaptics_ts_data *ts, const char *fw_path) {

	int ret = 0;
	const struct firmware *fw_entry = NULL;

	if ((ret = request_firmware(&fw_entry, fw_path,
					&ts->client->dev)) != 0) {
		TOUCH_ERR_MSG("request_firmware() failed %d\n", ret);
		goto error;
	}

	my_image_size = fw_entry->size;
	if (my_image_bin == NULL)
		my_image_bin = kzalloc(sizeof(char) * (my_image_size + 1), GFP_KERNEL);
	if (my_image_bin == NULL) {
		TOUCH_ERR_MSG("Can not allocate  memory\n");
		ret = -ENOMEM;
		goto error;
	}

	memcpy(my_image_bin, fw_entry->data, my_image_size);

	/* for checksum */
	*(my_image_bin+my_image_size) = 0xFF;

	strncpy(ts->fw_info.fw_image_product_id, &my_image_bin[0x0010], 6);
	strncpy(ts->fw_info.fw_image_version, &my_image_bin[0x11100], 4);

	ts->fw_info.fw_start = (unsigned char *)&my_image_bin[0];
	ts->fw_info.fw_size = my_image_size;

	CompleteReflash(ts);
	memset(my_image_bin, 0, my_image_size);

	release_firmware(fw_entry);
	kfree(my_image_bin);
	my_image_bin = NULL;
	return ret;
error:
	if (fw_entry)
		release_firmware(fw_entry);
	if (my_image_bin) {
		kfree(my_image_bin);
		my_image_bin = NULL;
	}
	return ret;
}

int FirmwareRecovery(struct synaptics_ts_data *ts, const char *fw_path)
{
	int ret = 0;
	const struct firmware *fw_entry = NULL;

	if ((ret = request_firmware(&fw_entry, fw_path,
			&ts->client->dev)) != 0) {
		TOUCH_ERR_MSG("request_firmware() failed %d\n", ret);
		goto error;
	}

	my_image_size = fw_entry->size;
	if (my_image_bin == NULL)
	    my_image_bin = kzalloc(sizeof(char) * (my_image_size + 1), GFP_KERNEL);
	if (my_image_bin == NULL) {
		TOUCH_ERR_MSG("Memory allocation failed\n");
		ret = -ENOMEM;
		goto error;
	}

	memcpy(my_image_bin, fw_entry->data, my_image_size);

	ts->fw_info.fw_start = (unsigned char *)&my_image_bin[0];
	ts->fw_info.fw_size = my_image_size;

	FlashRecovery(ts);
	memset(my_image_bin, 0, my_image_size);

	release_firmware(fw_entry);
	kfree(my_image_bin);
	my_image_bin = NULL;
	return ret;

error:
	if (fw_entry)
		release_firmware(fw_entry);
	if (my_image_bin) {
		kfree(my_image_bin);
		my_image_bin = NULL;
	}
	return ret;
}

static int writeRMI(struct i2c_client *client,
		u8 uRmiAddress, u8 *data, unsigned int length)
{
	return touch_i2c_write(client, uRmiAddress, length, data);
}

static int readRMI(struct i2c_client *client,
		u8 uRmiAddress, u8 *data, unsigned int length)
{
	return touch_i2c_read(client, uRmiAddress, length, data);
}

bool CheckFlashStatus(struct synaptics_ts_data *ts,
		enum FlashCommand command)//no ds4
{
	unsigned char uData = 0;
	// Read the "Program Enabled" bit of the F34 Control register,
	// and proceed only if the
	// bit is set.
	readRMI(ts->client, SynaF34_FlashStatus, &uData, 1);
	// TOUCH_INFO_MSG("SynaF34_FlashStatus=[%x], uData=[%x]\n",
	// SynaF34_FlashStatus, uData);

	//if ((uData & 0x3F) != 0)
	//TOUCH_ERR_MSG( "Command %s failed.\n\tFlash status : 0x%X\n",
	//SynaFlashCommandStr[command], uData & 0x3F);
	return !(uData & 0x3F);
}

void SynaImageParser(struct synaptics_ts_data *ts)//no ds4
{

	// img file parsing
	SynaImageSize = ((unsigned int)my_image_bin[0x08] |
			(unsigned int)my_image_bin[0x09] << 8 |
			(unsigned int)my_image_bin[0x0A] << 16 |
			(unsigned int)my_image_bin[0x0B] << 24);

	SynafirmwareImgData = (unsigned char *)((&my_image_bin[0]) + 0x100);

	// td4191
	SynaDisplayConfigImgStartAddr = ((unsigned int)my_image_bin[0x40] |
			(unsigned int)my_image_bin[0x41] << 8 |
			(unsigned int)my_image_bin[0x42] << 16 |
			(unsigned int)my_image_bin[0x43] << 24);

	SynaDisplayConfigImgData = (unsigned char *)((&my_image_bin[0]) + SynaDisplayConfigImgStartAddr);

	// for debugging
	TOUCH_DEBUG(DEBUG_BASE_INFO, "%s: Retrieve SynaDisplayConfigImgData from image offset at 0x%s\n",
		__func__, SynaDisplayConfigImgData );

	SynaconfigImgData  =
		(unsigned char *)(SynafirmwareImgData + SynaImageSize);
	SynafirmwareImgVersion = (unsigned int)(my_image_bin[7]);

	switch (SynafirmwareImgVersion) {
	case 2:
		SynalockImgData = (unsigned char *)((&my_image_bin[0]) + 0xD0);
		break;
	case 3:
	case 4:
		SynalockImgData = (unsigned char *)((&my_image_bin[0]) + 0xC0);
		break;
	case 5:
	case 6:
		SynalockImgData = (unsigned char *)((&my_image_bin[0]) + 0xB0);
	default:
		break;
	}
}

void SynaBootloaderLock(struct synaptics_ts_data *ts)//no ds4
{
	unsigned short lockBlockCount;
	unsigned char uData[2] = {0};
	unsigned short uBlockNum;
	enum FlashCommand cmd;

	if (my_image_bin[0x1E] == 0) {
		TOUCH_ERR_MSG("Skip lockdown process with this .img\n");
		return;
	}
	// Check if device is in unlocked state
	readRMI(ts->client, (SynaF34QueryBase + 1), &uData[0], 1);

	//Device is unlocked
	if (uData[0] & 0x02) {
		TOUCH_ERR_MSG("Device unlocked. Lock it first...\n");
		// Different bootloader version has different block count
		// for the lockdown data
		// Need to check the bootloader version from the image file
		// being reflashed
		switch (SynafirmwareImgVersion) {
		case 2:
			lockBlockCount = 3;
			break;
		case 3:
		case 4:
			lockBlockCount = 4;
			break;
		case 5:
		case 6:
			lockBlockCount = 5;
			break;
		default:
			lockBlockCount = 0;
			break;
		}

		// Write the lockdown info block by block
		// This reference code of lockdown process does not check
		// for bootloader version
		// currently programmed on the ASIC against the bootloader
		// version of the image to
		// be reflashed. Such case should not happen in practice.
		// Reflashing cross different
		// bootloader versions is not supported.
		for (uBlockNum = 0; uBlockNum < lockBlockCount; ++uBlockNum) {
			uData[0] = uBlockNum & 0xff;
			uData[1] = (uBlockNum & 0xff00) >> 8;

			/* Write Block Number */
			writeRMI(ts->client,
					SynaF34Reflash_BlockNum, &uData[0], 2);

			/* Write Data Block */
			writeRMI(ts->client, SynaF34Reflash_BlockData,
					SynalockImgData, SynaFirmwareBlockSize);

			/* Move to next data block */
			SynalockImgData += SynaFirmwareBlockSize;

			/* Issue Write Lockdown Block command */
			cmd = m_uF34ReflashCmd_LockDown;
			writeRMI(ts->client, SynaF34_FlashControl,
					(unsigned char *)&cmd, 1);

			/* Wait ATTN until device is done writing the block
			 * and is ready for the next. */
			SynaWaitForATTN(1000, ts);
			CheckFlashStatus(ts, cmd);
		}

		// Enable reflash again to finish the lockdown process.
		// Since this lockdown process is part of the reflash process,
		// we are enabling
		// reflash instead, rather than resetting the device
		// to finish the unlock procedure.
		SynaEnableFlashing(ts);
	} else
		TOUCH_ERR_MSG("Device already locked.\n");
}

// This function is to check the touch controller type of the touch controller
// matches with the firmware image
bool CheckTouchControllerType(struct synaptics_ts_data *ts)
{
	int ID;
	char buffer[5] = {0};
	char controllerType[20] = {0};
	unsigned char uData[4] = {0};

	readRMI(ts->client, (SynaF01QueryBase + 22),
			&SynaF01Query43Length, 1); //43

	if ((SynaF01Query43Length & 0x0f) > 0) {
		readRMI(ts->client, (SynaF01QueryBase + 23), &uData[0], 1);
		if (uData[0] & 0x01) {
			readRMI(ts->client, (SynaF01QueryBase + 17),
					&uData[0], 2);

			ID = ((int)uData[0] | ((int)uData[1] << 8));

			if (strstr(controllerType, buffer) != 0)
				return true;
			return false;
		} else
			return false;
	} else
		return false;
}

/*bool CheckFimrwareRevision(struct synaptics_ts_data *ts)
  {
  unsigned char uData[16];
  char revision[106] = {0};
  int i;
  readRMI(ts->client, (SynaF01QueryBase + 28 + SynaF01Query43Length),
  &uData[0], 16);

  for (i = 0; i < 0; i++)
  {
  while (uData[i] != '\0')
  {
  revision[i] = (char)uData[0];
  }
  }

  if (strcmp(revision, FW_REVISION) == 0)
  return true;
  return false;
  }*/


/* SynaScanPDT scans the Page Description Table (PDT)
 * and sets up the necessary variables
 * for the reflash process. This function is a "slim" version of the PDT scan
 * function in
 * in PDT.c, since only F34 and F01 are needed for reflash.
 */
int SynaScanPDT(struct synaptics_ts_data *ts) //void SynaSetup()
{
	unsigned char address;
	unsigned char uData[2] = {0}; //no ds4
	unsigned char buffer[6] = {0};

	ts->ubootloader_mode = false;

	for (address = 0xe9; address > 0xc0; address = address - 6) {
		if(readRMI(ts->client, address, buffer, 6) == -EIO) {
			TOUCH_ERR_MSG("Fail I2C Read in Scan PDT\n");
			return ERROR;
		}

		if (!buffer[5])
			continue;
		switch (buffer[5]) {
		case 0x35:
			SynaF35QueryBase = buffer[0];
			SynaF35ControlBase = buffer[2];
			SynaF35DataBase = buffer[3];
			ts->ubootloader_mode = true;
			return NO_ERROR;
		case 0x34:
			SynaF34DataBase = buffer[3];
			SynaF34QueryBase = buffer[0];
			break;
		case 0x01:
			SynaF01DataBase = buffer[3];
			SynaF01ControlBase = buffer[2];
			SynaF01CommandBase = buffer[1];
			SynaF01QueryBase = buffer[0];//no ds4
			break;
		}
	}

	SynaF34Reflash_BlockNum = SynaF34DataBase;
	SynaF34Reflash_BlockData = SynaF34DataBase + 1; //+2
	SynaF34ReflashQuery_BootID = SynaF34QueryBase;
	SynaF34ReflashQuery_FlashPropertyQuery = SynaF34QueryBase + 1;//+2
	SynaF34ReflashQuery_BlockSize = SynaF34QueryBase + 2;//+3
	SynaF34ReflashQuery_FirmwareBlockCount = SynaF34QueryBase + 3;//+5
	//SynaF34ReflashQuery_ConfigBlockSize = SynaF34QueryBase + 3
	SynaF34_FlashControl = SynaF34DataBase + 2;
	SynaF34_FlashStatus = SynaF34DataBase + 3;//no ds4

	//TD4191: add information of SynaDisplayBlockCount, retrieved from f34 query registers
	readRMI(ts->client, SynaF34ReflashQuery_FirmwareBlockCount, buffer, 6);
	SynaFirmwareBlockCount  = buffer[0] | (buffer[1] << 8);
	SynaConfigBlockCount    = buffer[2] | (buffer[3] << 8);
	SynaDisplayBlockCount   = buffer[4] | (buffer[5] << 8);

	TOUCH_DEBUG(DEBUG_BASE_INFO, "%s : SynaFirmwareBlockCount = 0x%04x, SynaConfigBlockCount = 0x%04x, SynaDisplayBlockCount = 0x%04x\n",
			__func__,SynaFirmwareBlockCount, SynaConfigBlockCount,SynaDisplayBlockCount);

	readRMI(ts->client, SynaF34ReflashQuery_BlockSize, &uData[0], 2);

	SynaConfigBlockSize = uData[0] | (uData[1] << 8);
	SynaFirmwareBlockSize = uData[0] | (uData[1] << 8);
	SynaDisplayBlockSize = uData[0] | (uData[1] << 8);
	// Flash block size sould always get 16 for all partition
	TOUCH_DEBUG(DEBUG_BASE_INFO, "%s : SynaDisplayBlockSize = 0x%04x\n",__func__, SynaDisplayBlockSize);


	//cleat ATTN
	readRMI(ts->client, (SynaF01DataBase + 1), buffer, 1);

	return NO_ERROR;
}

/* SynaInitialize sets up the reflahs process
 */
void SynaInitialize(struct synaptics_ts_data *ts)
{
	u8 data;

	TOUCH_ERR_MSG("Initializing Reflash Process...\n");

	data = 0x00;
	writeRMI(ts->client, 0xff, &data, 1);

	SynaScanPDT(ts);	//SynaSetup();

	if (!ts->ubootloader_mode)
		SynaImageParser(ts); //no ds4
}

/* SynaReadFirmwareInfo reads the F34 query registers and retrieves the block
 * size and count
 * of the firmware section of the image to be reflashed
 */
void SynaReadFirmwareInfo(struct synaptics_ts_data *ts)
{
	unsigned char uData[3] = {0};
	unsigned char product_id[11];
	int firmware_version;

	TOUCH_INFO_MSG("%s\n", __FUNCTION__);


	readRMI(ts->client, SynaF01QueryBase + 11, product_id, 10);
	product_id[10] = '\0';
	TOUCH_ERR_MSG("Read Product ID %s\n", product_id);

	readRMI(ts->client, SynaF01QueryBase + 18, uData, 3);
	firmware_version = uData[2] << 16 | uData[1] << 8 | uData[0];
	TOUCH_ERR_MSG("Read Firmware Info %d\n", firmware_version);

	CheckTouchControllerType(ts);
	/*CheckFimrwareRevision(ts);*/
}
// no void SynaReadConfigInfo()
/* SynaReadBootloadID reads the F34 query registers and retrieves
 * the bootloader ID of the firmware
 */
void SynaReadBootloadID(struct synaptics_ts_data *ts)
{
	unsigned char uData[2] = {0};

	readRMI(ts->client, SynaF34ReflashQuery_BootID, &uData[0], 2);
	SynaBootloadID = uData[0] | (uData[1] << 8);
	TOUCH_ERR_MSG("SynaBootloadID = %d\n", SynaBootloadID);
}

/* SynaWriteBootloadID writes the bootloader ID to the F34 data register
 * to unlock the reflash process
 */
void SynaWriteBootloadID(struct synaptics_ts_data *ts)
{
	unsigned char uData[2];

	uData[0] = SynaBootloadID % 0x100;
	uData[1] = SynaBootloadID / 0x100;

	TOUCH_ERR_MSG("uData[0] = %x uData[0] = %x\n", uData[0], uData[1]);
	writeRMI(ts->client, SynaF34Reflash_BlockData, &uData[0], 2);
}

/* SynaEnableFlashing kicks off the reflash process
 */
void SynaEnableFlashing(struct synaptics_ts_data *ts)
{
	//    int ret;
	unsigned char uStatus = 0;
	unsigned char zero = 0x00;
	enum FlashCommand cmd;
	unsigned char uData[3] = {0};
	int firmware_version;

	TOUCH_INFO_MSG("%s\n", __FUNCTION__);

	TOUCH_INFO_MSG("\nEnable Reflash...\n");
	readRMI (ts->client, SynaF01DataBase, &uStatus, 1);

	if ((uStatus & 0x40) == 0) {
		writeRMI(ts->client, SynaF01ControlBase + 1, &zero, 1);
		msleep(20);

		// Reflash is enabled by first reading the bootloader ID
		// from the firmware and write it back
		SynaReadBootloadID(ts);
		SynaWriteBootloadID(ts);

		// Write the "Enable Flash Programming command
		// to F34 Control register
		// Wait for ATTN and then clear the ATTN.
		cmd = m_uF34ReflashCmd_Enable;
		writeRMI(ts->client, SynaF34_FlashControl,
				(unsigned char *)&cmd, 1);

		msleep(100);

		SynaWaitForATTN(1000, ts);

		//I2C addrss may change
		//ConfigCommunication();

		// Scan the PDT again to ensure all register offsets are correct
		SynaScanPDT(ts);

		readRMI(ts->client, SynaF01QueryBase + 18, uData, 3);
		firmware_version = uData[2] << 16 | uData[1] << 8 | uData[0];

		// Read the "Program Enabled" bit of the F34 Control register,
		// and proceed only if the
		// bit is set.
		CheckFlashStatus(ts, cmd);
	}
}

/* SynaWaitForATTN waits for ATTN to be asserted within a certain time
 * threshold.
 */
unsigned int SynaWaitForATTN(int timeout, struct synaptics_ts_data *ts)
{
	unsigned char uStatus;
	//int duration = 50;
	//int retry = timeout/duration;
	//int times = 0;

	int trial_us = 0;
#ifdef POLLING
	do {
		uStatus = 0x00;
		readRMI(ts->client, (SynaF01DataBase + 1), &uStatus, 1);
		if (uStatus != 0)
			break;
		Sleep(duration);
		times++;
	} while (times < retry);

	if (times == retry)
		return -1;
#else
	/*if (Line_WaitForAttention(timeout) == EErrorTimeout)
	  {
	  return -1;
	  }*/
	while ((gpio_get_value(ts->pdata->int_pin) != 0)
			&& (trial_us < (timeout * 1000))) {
		udelay(1);
		trial_us++;
	}
	if (gpio_get_value(ts->pdata->int_pin) != 0) {
		TOUCH_ERR_MSG("interrupt pin is busy...\n");
		return -1;
	}

	readRMI(ts->client, (SynaF01DataBase + 1), &uStatus, 1);
#endif
	return 0;
}
/* SynaFinalizeReflash finalizes the reflash process
 */
void SynaFinalizeReflash(struct synaptics_ts_data *ts)
{
	unsigned char uData;

	TOUCH_INFO_MSG("%s\n", __FUNCTION__);

	TOUCH_INFO_MSG("Finalizing Reflash...\n");

	// Issue the "Reset" command to F01 command register to reset the chip
	// This command will also test the new firmware image and checki
	// if its is valid
	uData = 1;
	writeRMI(ts->client, SynaF01CommandBase, &uData, 1);

	// After command reset, there will be 2 interrupt to be asserted
	// Simply sleep 150 ms to skip first attention
	msleep(150);
	SynaWaitForATTN(1000, ts);

	SynaScanPDT(ts);

	readRMI(ts->client, SynaF01DataBase, &uData, 1);
}

/* SynaFlashFirmwareWrite writes the firmware section
 * of the image block by block
 */
void SynaFlashFirmwareWrite(struct synaptics_ts_data *ts)
{
	//unsigned char *puFirmwareData = (unsigned char *)&my_image_bin[0x100];
	unsigned char *puFirmwareData = SynafirmwareImgData;
	unsigned char uData[2];
	unsigned short blockNum;
	enum FlashCommand cmd;

	for (blockNum = 0; blockNum < SynaFirmwareBlockCount; ++blockNum) {
		if (blockNum == 0) {

			//Block by blcok, write the block number and data
			//to the corresponding F34 data registers
			uData[0] = blockNum & 0xff;
			uData[1] = (blockNum & 0xff00) >> 8;
			writeRMI(ts->client, SynaF34Reflash_BlockNum,
					&uData[0], 2);
		}

		writeRMI(ts->client, SynaF34Reflash_BlockData, puFirmwareData,
				SynaFirmwareBlockSize);
		puFirmwareData += SynaFirmwareBlockSize;

		// Issue the "Write Firmware Block" command
		cmd = m_uF34ReflashCmd_FirmwareWrite;
		writeRMI(ts->client, SynaF34_FlashControl,
				(unsigned char *)&cmd, 1);

		//SynaWaitForATTN(1000,ts);
		CheckFlashStatus(ts, cmd);
		//TOUCH_INFO_MSG("[%s] blockNum=[%d],
		//SynaFirmwareBlockCount=[%d]\n", __FUNCTION__,
		//blockNum, SynaFirmwareBlockCount);
#ifdef SHOW_PROGRESS
		if (blockNum % 100 == 0)
			TOUCH_ERR_MSG("blk %d / %d\n",
					blockNum, SynaFirmwareBlockCount);
#endif
	}
#ifdef SHOW_PROGRESS
	TOUCH_ERR_MSG("blk %d / %d\n",
			SynaFirmwareBlockCount, SynaFirmwareBlockCount);
#endif
}

/* SynaFlashFirmwareWrite writes the firmware section
 * of the image block by block
 */
void SynaFlashConfigWrite(struct synaptics_ts_data *ts)
{
	//unsigned char *puConfigData = (unsigned char *)&my_image_bin[0x100];
	unsigned char *puConfigData = SynaconfigImgData;
	unsigned char uData[2];
	unsigned short blockNum;
	enum FlashCommand cmd;

	for (blockNum = 0; blockNum < SynaConfigBlockCount; ++blockNum)	{
		//Block by blcok, write the block number and data
		//to the corresponding F34 data registers
		uData[0] = blockNum & 0xff;
		uData[1] = (blockNum & 0xff00) >> 8;
		writeRMI(ts->client, SynaF34Reflash_BlockNum, &uData[0], 2);

		writeRMI(ts->client, SynaF34Reflash_BlockData,
				puConfigData, SynaConfigBlockSize);
		puConfigData += SynaConfigBlockSize;

		// Issue the "Write Config Block" command
		cmd = m_uF34ReflashCmd_ConfigWrite;
		writeRMI(ts->client, SynaF34_FlashControl,
				(unsigned char *)&cmd, 1);

		SynaWaitForATTN(100, ts);
		CheckFlashStatus(ts, cmd);
#ifdef SHOW_PROGRESS
		if (blockNum % 100 == 0)
			TOUCH_ERR_MSG("blk %d / %d\n",
					blockNum, SynaConfigBlockCount);
#endif
	}
#ifdef SHOW_PROGRESS
	TOUCH_ERR_MSG("blk %d / %d\n",
			SynaConfigBlockCount, SynaConfigBlockCount);
#endif
}

/*
	td4191:
	we erase all patition of in the begin of update process,
	need to write both touch config and display config to data into IC flash
*/
void SynaFlashDispConfigWrite(struct synaptics_ts_data *ts)
{
	unsigned char *dispConfigData = SynaDisplayConfigImgData;
	unsigned char uData[2];
	unsigned short blockNum;
	enum FlashCommand cmd;

	for (blockNum = 0; blockNum < SynaDisplayBlockCount; ++blockNum)	{
		//Block by blcok, write the block number and data
		//to the corresponding F34 data registers
		uData[0] = blockNum & 0xff;
		uData[1] = (blockNum & 0xff00) >> 8;
		uData[1] |= 0x60; //0x60: config blcok select mask
		writeRMI(ts->client, SynaF34Reflash_BlockNum, &uData[0], 2);

		writeRMI(ts->client, SynaF34Reflash_BlockData,
				dispConfigData, SynaDisplayBlockSize);
		dispConfigData += SynaDisplayBlockSize;

		// Issue the "Write Config Block" command
		cmd = m_uF34ReflashCmd_ConfigWrite;
		writeRMI(ts->client, SynaF34_FlashControl,
				(unsigned char *)&cmd, 1);

		SynaWaitForATTN(100, ts);
		CheckFlashStatus(ts, cmd);
#ifdef SHOW_PROGRESS
		if (blockNum % 100 == 0)
			TOUCH_ERR_MSG("blk %d / %d\n",
					blockNum, SynaDisplayBlockCount);
#endif
	}
#ifdef SHOW_PROGRESS
	TOUCH_ERR_MSG("blk %d / %d\n",
			SynaDisplayBlockCount, SynaDisplayBlockCount);
#endif
}

/* EraseConfigBlock erases the config block
 */
void eraseAllBlock(struct synaptics_ts_data *ts)
{
	enum FlashCommand cmd;

	// Erase of config block is done by first entering into bootloader mode
	SynaReadBootloadID(ts);
	SynaWriteBootloadID(ts);

	// Command 7 to erase config block
	cmd = m_uF34ReflashCmd_EraseAll;
	writeRMI(ts->client, SynaF34_FlashControl, (unsigned char *)&cmd, 1);

	SynaWaitForATTN(6000, ts);
	CheckFlashStatus(ts, cmd);
}

/* SynaProgramFirmware prepares the firmware writing process
 */
void SynaProgramFirmware(struct synaptics_ts_data *ts)
{
	TOUCH_ERR_MSG("\nProgram Firmware Section...\n");

	eraseAllBlock(ts);

	SynaFlashFirmwareWrite(ts);

	SynaFlashConfigWrite(ts);

	SynaFlashDispConfigWrite(ts);	//td4191

}

/* SynaProgramFirmware prepares the firmware writing process
 */
void SynaUpdateConfig(struct synaptics_ts_data *ts)
{
	TOUCH_ERR_MSG("\nUpdate Config Section...\n");

	EraseConfigBlock(ts);

	SynaFlashConfigWrite(ts);

	SynaFlashDispConfigWrite(ts);
}



/* EraseConfigBlock erases the config block
 */
void EraseConfigBlock(struct synaptics_ts_data *ts)
{
	enum FlashCommand cmd;

	// Erase of config block is done by first entering into bootloader mode
	SynaReadBootloadID(ts);
	SynaWriteBootloadID(ts);

	// Command 7 to erase config block
	cmd = m_uF34ReflashCmd_EraseUIConfig;
	writeRMI(ts->client, SynaF34_FlashControl, (unsigned char *)&cmd, 1);

	SynaWaitForATTN(2000, ts);
	CheckFlashStatus(ts, cmd);
}

void SynaCheckFlashStatus(struct synaptics_ts_data *ts)
{
	unsigned char status;

	readRMI(ts->client, SynaF35DataBase + F35_ERROR_CODE_OFFSET, &status, 1);

	status = status & 0x7f;

	if (status != 0x00)
		TOUCH_ERR_MSG("Recovery mode error code = 0x%02x\n", status);

	return;
}

void SynaEraseFlash(struct synaptics_ts_data *ts)
{
	enum F35RecoveryCommand command = CMD_F35_ERASE_ALL;

	writeRMI(ts->client, SynaF35ControlBase + F35_CHUNK_COMMAND_OFFSET,
			(unsigned char *)&command, 1);

	msleep(F35_ERASE_ALL_WAIT_MS);

	SynaCheckFlashStatus(ts);

	return;
}

void SynaWriteChunkData(struct synaptics_ts_data *ts)
{
	unsigned char chunk_number[] = {0, 0};
	unsigned char chunk_spare;
	unsigned char chunk_size;
	unsigned char buf[F35_CHUNK_SIZE + 1];
	unsigned short chunk;
	unsigned short chunk_total;
	unsigned char *chunk_ptr = (unsigned char *)ts->fw_info.fw_start;

	writeRMI(ts->client, SynaF35ControlBase + F35_CHUNK_NUM_LSB_OFFSET,
			chunk_number, sizeof(chunk_number));

	chunk_total = ts->fw_info.fw_size / F35_CHUNK_SIZE;
	chunk_spare = ts->fw_info.fw_size % F35_CHUNK_SIZE;
	if (chunk_spare)
		chunk_total++;

	buf[sizeof(buf) - 1] = CMD_F35_WRITE_CHUNK;

	for (chunk = 0; chunk < chunk_total; chunk++) {
		if (chunk_spare && chunk == (chunk_total - 1))
			chunk_size = chunk_spare;
		else
			chunk_size = F35_CHUNK_SIZE;

		memset(buf, 0x00, F35_CHUNK_SIZE);
		memcpy(buf, chunk_ptr, chunk_size);

		writeRMI(ts->client, SynaF35ControlBase + F35_CHUNK_DATA_OFFSET,
				buf, sizeof(buf));

		chunk_ptr += chunk_size;
#ifdef SHOW_PROGRESS
		if (chunk % 100 == 0)
			TOUCH_ERR_MSG("[Recovery] %d / %d\n", chunk, chunk_total);
#endif
	}
#ifdef SHOW_PROGRESS
	TOUCH_ERR_MSG("[Recovery] %d / %d\n", chunk, chunk_total);
#endif
	SynaCheckFlashStatus(ts);

	return;
}

void SynaFinalizeRecovery(struct synaptics_ts_data *ts)
{
	unsigned char uData;
	enum F35RecoveryCommand command = CMD_F35_RESET;

	writeRMI(ts->client, SynaF35ControlBase + F35_CHUNK_COMMAND_OFFSET,
			(unsigned char *)&command, 1);

	msleep(F35_RESET_WAIT_MS);

	SynaWaitForATTN(1000, ts);

	SynaScanPDT(ts);

	readRMI(ts->client, SynaF01DataBase, &uData, 1);
	TOUCH_ERR_MSG("[%s] FW Recovery Finished!!!!", __func__);
	return;
}

/* CompleteReflash reflashes the entire user image,
 * including the configuration block and firmware
 */
void CompleteReflash(struct synaptics_ts_data *ts)
{
	bool bFlashAll = true;

	SynaInitialize(ts);

	SynaReadFirmwareInfo(ts);

	SynaEnableFlashing(ts);

	SynaBootloaderLock(ts);

	if (bFlashAll)
		SynaProgramFirmware(ts);
	else
		SynaUpdateConfig(ts);

	SynaFinalizeReflash(ts);

        if (ts->fw_info.need_rewrite_firmware)
	        mdss_lcd_lut_update();
}

void FlashRecovery(struct synaptics_ts_data *ts)
{
	SynaInitialize(ts);

	SynaEraseFlash(ts);

	SynaWriteChunkData(ts);

	SynaFinalizeRecovery(ts);

	mdss_lcd_lut_update();

	return;
}
