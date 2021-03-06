/**
 * @file   MPU-9250.c
 * @brief  9-axis sensor(InvenSense MPU-9250) driver for TZ10xx.
 * @date   2016/03/30 ADD functions.
 * @author Cerevo Inc.
 */

/*
Copyright 2015-2016 Cerevo Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

/** Includes **/
#include <stdint.h>
#include <stdbool.h>
/* MCU support. */
#include "TZ10xx.h"
#include "PMU_TZ10xx.h"
#include "GPIO_TZ10xx.h"
#include "SPI_TZ10xx.h"

#include "utils.h"
#include "MPU-9250.h"

extern TZ10XX_DRIVER_PMU  Driver_PMU;
extern TZ10XX_DRIVER_GPIO Driver_GPIO;
static TZ10XX_DRIVER_SPI *tz10xx_drv_spi = NULL;

static uint8_t magnetometer_calib[3];
static float gyro_div;
static float accel_div;

typedef enum {
    MPU9250_STAT_NONE = 0,
    MPU9250_STAT_IDLE,
    MPU9250_STAT_MAESUREING,
} MPU9250_STAT;

static MPU9250_STAT stat = MPU9250_STAT_NONE;

/*
 * Read byte data from SPI
 */
static bool mpu9250_drv_read_byte(uint8_t addr, uint8_t *val)
{
    uint16_t frame;

    addr |= 0x80;       /* set read flag. */
    frame = addr << 8;  /* build send frame. */
    frame = tz10xx_drv_spi->TransferFrame(frame);

    *val = frame & 0xff;

    return true;
}

/*
 * Write byte data to SPI
 */
static uint16_t mpu9250_drv_write_byte(uint8_t addr, uint8_t val)
{
    uint16_t frame;

    frame = addr << 8 | val;
    frame = tz10xx_drv_spi->TransferFrame(frame);

    return frame;
}

/*  API FUNCTIONS  */
static void (*event_handler)(uint8_t);

static void intr_handler(uint32_t pin)
{
    uint8_t evt;
    if (event_handler != NULL) {
        mpu9250_drv_read_byte(MPU9250_REG_INT_STATUS, &evt);
        event_handler(evt);
    }
}


void MPU9250_drv_enable_events(void (*func)(uint8_t), int intr_io, uint8_t event)
{
    Driver_PMU.StandbyInputBuffer(PMU_IO_FUNC_GPIO_5, 0);
    Driver_GPIO.Configure(5, GPIO_DIRECTION_INPUT_HI_Z, GPIO_EVENT_EDGE_NEG, intr_handler);

    mpu9250_drv_write_byte(MPU9250_REG_INT_PIN_CFG, 0x00);
    mpu9250_drv_write_byte(MPU9250_REG_INT_ENABLE, event);

    event_handler = func;
}

void MPU9250_drv_disable_events(void)
{
    event_handler = NULL;
    mpu9250_drv_write_byte(MPU9250_REG_INT_ENABLE, 0x00);
    Driver_GPIO.Configure(5, GPIO_DIRECTION_INPUT_HI_Z, GPIO_EVENT_DISABLE, NULL);
}

/*
 * Initialize MPU9250 9axis sensor.
 * see: MPU-9250 Product Specification 7.5 SPI interface.
 */
bool MPU9250_drv_init(TZ10XX_DRIVER_SPI *spi_drv)
{
    uint8_t val = 0x00;

    if (spi_drv == NULL) {
        return false;
    }

    tz10xx_drv_spi = spi_drv;
    tz10xx_drv_spi->Initialize(NULL);
    /* SPI Mode0, MSB to LSB */
    tz10xx_drv_spi->Configure(ARM_SPI_CPOL0_CPHA0, ARM_SPI_MSB_LSB);
    /* FrameSize xxbit */
    tz10xx_drv_spi->FrameSize(15);
    /* Clock 1MHz */
    tz10xx_drv_spi->BusSpeed(1000000);

    tz10xx_drv_spi->PowerControl(ARM_POWER_FULL);


    /* MPU9250 reset & initial */
    for (int i = 0; i < 100; i++) {
        mpu9250_drv_read_byte(MPU9250_REG_WHO_AM_I, &val);
        if (val == 0x71) {
            break;
        }
        Usleep(100000);  /* 100ms */
    }
    if (val != 0x71) {
        return false;
    }

    uint8_t init_conf[][2] = {
        {MPU9250_REG_PWR_MGMT_1,    0x80},  /* reset */
        {MPU9250_REG_PWR_MGMT_1,    0x40},  /* sleep */
        {MPU9250_REG_PWR_MGMT_2,    0x3f},  /* Disable Accel & Gyro */
        {MPU9250_REG_INT_PIN_CFG,   0x30},  /* Pin config */
        {0xff,                      0xff}
    };
    for (int i = 0; init_conf[i][0] != 0xff; i++) {
        mpu9250_drv_write_byte(init_conf[i][0], init_conf[i][1]);
        Usleep(1000);
    }

    stat = MPU9250_STAT_IDLE;   /* Update STATE */
    return true;
}

/*
 * Sleep devive.
 */
bool MPU9250_drv_sleep(void)
{
    uint8_t reg_pwr_mgmt_1;
    /* Set sleep bit */
    mpu9250_drv_read_byte(MPU9250_REG_PWR_MGMT_1, &reg_pwr_mgmt_1);
    reg_pwr_mgmt_1 |= 0x40;
    mpu9250_drv_write_byte(MPU9250_REG_PWR_MGMT_1, reg_pwr_mgmt_1);

    return true;
}

/*
 * Wakeup device.
 */
bool MPU9250_drv_wakeup(void)
{
    uint8_t reg_pwr_mgmt_1;
    /* Cler sleep bit */
    mpu9250_drv_read_byte(MPU9250_REG_PWR_MGMT_1, &reg_pwr_mgmt_1);
    reg_pwr_mgmt_1 &= ~0x40;
    mpu9250_drv_write_byte(MPU9250_REG_PWR_MGMT_1, reg_pwr_mgmt_1);

    return true;
}

/*
 * Start Gyro.
 */
bool MPU9250_drv_start_gyro(MPU9250_BIT_GYRO_FS_SEL gyro_fs, MPU9250_BIT_DLPF_CFG dlpf_cfg)
{
    uint8_t reg_pwr_mgmt_1;
    uint8_t reg_pwr_mgmt_2;

    mpu9250_drv_read_byte(MPU9250_REG_PWR_MGMT_1, &reg_pwr_mgmt_1);
    if (reg_pwr_mgmt_1 & 0x40) {
        /* Device is sleeping... */
        return false;
    }

    /* Enable Gyro */
    mpu9250_drv_read_byte(MPU9250_REG_PWR_MGMT_2, &reg_pwr_mgmt_2);
    reg_pwr_mgmt_2 &= ~0x07;
    mpu9250_drv_write_byte(MPU9250_REG_PWR_MGMT_2, reg_pwr_mgmt_2);
    /* Gyro LPF */
    mpu9250_drv_write_byte(MPU9250_REG_CONFIG, (uint8_t)dlpf_cfg);
    /* Gyro configuration */
    mpu9250_drv_write_byte(MPU9250_REG_GYRO_CONFIG, (uint8_t)gyro_fs);

    switch (gyro_fs) {
    case MPU9250_BIT_GYRO_FS_SEL_250DPS:
        gyro_div = 131.0;
        break;
    case MPU9250_BIT_GYRO_FS_SEL_500DPS:
        gyro_div = 65.5;
        break;
    case MPU9250_BIT_GYRO_FS_SEL_1000DPS:
        gyro_div = 32.8;
        break;
    case MPU9250_BIT_GYRO_FS_SEL_2000DPS:
        gyro_div = 16.4;
        break;
    default:
        gyro_div = 131.0;
        break;
    }

    return true;
}

/*
 * Stop Gyro.
 */
bool MPU9250_drv_stop_gyro(void)
{
    uint8_t reg_pwr_mgmt_2;

    /* Disable Gyro */
    mpu9250_drv_read_byte(MPU9250_REG_PWR_MGMT_2, &reg_pwr_mgmt_2);
    reg_pwr_mgmt_2 |= 0x07;
    mpu9250_drv_write_byte(MPU9250_REG_PWR_MGMT_2, reg_pwr_mgmt_2);

    return true;
}

/*
 * Start Accelerometer.
 */
bool MPU9250_drv_start_accel(MPU9250_BIT_ACCEL_FS_SEL accel_fs, MPU9250_BIT_A_DLPFCFG a_dlpfcfg)
{
    uint8_t reg_pwr_mgmt_1;
    uint8_t reg_pwr_mgmt_2;

    mpu9250_drv_read_byte(MPU9250_REG_PWR_MGMT_1, &reg_pwr_mgmt_1);
    if (reg_pwr_mgmt_1 & 0x40) {
        /* Device is sleeping... */
        return false;
    }

    /* Enable Accel */
    mpu9250_drv_read_byte(MPU9250_REG_PWR_MGMT_2, &reg_pwr_mgmt_2);
    reg_pwr_mgmt_2 &= ~0x38;
    mpu9250_drv_write_byte(MPU9250_REG_PWR_MGMT_2, reg_pwr_mgmt_2);
    /* Accel LPF */
    mpu9250_drv_write_byte(MPU9250_REG_ACCEL_CONFIG2, 0x08 | (uint8_t)a_dlpfcfg);
    /* Accel configuration */
    mpu9250_drv_write_byte(MPU9250_REG_ACCEL_CONFIG,  (uint8_t)accel_fs);

    switch (accel_fs) {
    case MPU9250_BIT_ACCEL_FS_SEL_2G:
        accel_div = 16384;
        break;
    case MPU9250_BIT_ACCEL_FS_SEL_4G:
        accel_div = 8192;
        break;
    case MPU9250_BIT_ACCEL_FS_SEL_8G:
        accel_div = 4096;
        break;
    case MPU9250_BIT_ACCEL_FS_SEL_16G:
        accel_div = 2048;
        break;
    default:
        accel_div = 16384;
        break;
    }

    return true;
}

/*
 * Stop Accelerometer.
 */
bool MPU9250_drv_stop_accel(void)
{
    uint8_t reg_pwr_mgmt_2;

    /* Disable Accel */
    mpu9250_drv_read_byte(MPU9250_REG_PWR_MGMT_2, &reg_pwr_mgmt_2);
    reg_pwr_mgmt_2 |= 0x38;
    mpu9250_drv_write_byte(MPU9250_REG_PWR_MGMT_2, reg_pwr_mgmt_2);

    return true;
}

/*
 * Start Mangetometer.
 */
bool MPU9250_drv_start_magnetometer(AK8963_BIT_CNTL1_MODE mode)
{
    uint8_t val;
    uint8_t reg_pwr_mgmt_1;
    uint8_t reg_user_ctrl;

    mpu9250_drv_read_byte(MPU9250_REG_PWR_MGMT_1, &reg_pwr_mgmt_1);
    if (reg_pwr_mgmt_1 & 0x40) {
        /* Device is sleeping... */
        return false;
    }

    /* I2C multi-master / 400kHz */
    mpu9250_drv_write_byte(MPU9250_REG_I2C_MST_CTRL, 0x0D);
    /* reset I2C master */
    mpu9250_drv_read_byte(MPU9250_REG_USER_CTRL, &reg_user_ctrl);
    reg_user_ctrl |= 0x02;
    mpu9250_drv_write_byte(MPU9250_REG_USER_CTRL, reg_user_ctrl);
    while (reg_user_ctrl & 0x02) {
        mpu9250_drv_read_byte(MPU9250_REG_USER_CTRL, &reg_user_ctrl);
    }
    /* I2C master mode enable */
    mpu9250_drv_read_byte(MPU9250_REG_USER_CTRL, &reg_user_ctrl);
    reg_user_ctrl |= 0x20;
    mpu9250_drv_write_byte(MPU9250_REG_USER_CTRL, reg_user_ctrl);
    /* reset I2C slave device */
    mpu9250_drv_read_byte(MPU9250_REG_USER_CTRL, &reg_user_ctrl);
    reg_user_ctrl |= 0x10;
    mpu9250_drv_write_byte(MPU9250_REG_USER_CTRL, reg_user_ctrl);

    /* Initialize AK8963(MPU9250 built in.) */
    /* Read `who am i' */
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_ADDR, AK8963_I2C_ADDR | 0x80);
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_REG,  AK8963_REG_WIA);
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_CTRL, 0x81);
    for (int i = 0; i < 100; i++) {
        Usleep(10000);
        mpu9250_drv_read_byte(MPU9250_REG_EXT_SENS_DATA_00, &val);
        if (val == 0x48) {
            break;
        }
    }
    if (val != 0x48) {
        return false;
    }

    /* Reset */
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_ADDR, AK8963_I2C_ADDR);
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_REG,  AK8963_REG_CNTL2);
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_DO,   0x01);
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_CTRL, 0x81);
    Usleep(10000);  /* 10ms */

    /* read calibration data */
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_ADDR, AK8963_I2C_ADDR | 0x80);
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_REG,  AK8963_REG_ASAX);
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_CTRL, 0x83);
    Usleep(10000);  /* 10ms */
    for (int i = 0; i < sizeof(magnetometer_calib); i++) {
        mpu9250_drv_read_byte(MPU9250_REG_EXT_SENS_DATA_00 + i, &magnetometer_calib[i]);
    }

    /* Set operation mode. */
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_ADDR, AK8963_I2C_ADDR);
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_REG,  AK8963_REG_CNTL1);
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_DO,   0x10 | (uint8_t)mode);
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_CTRL, 0x81);

    return true;
}

/*
 * Stop Magnetometer.
 */
bool MPU9250_drv_stop_magnetometer(void)
{
    uint8_t reg_user_ctrl;

    /* power down mode. */
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_ADDR, AK8963_I2C_ADDR);
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_REG,  AK8963_REG_CNTL1);
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_DO,   0x10);
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_CTRL, 0x81);

    /* I2C master mode disable */
    mpu9250_drv_read_byte(MPU9250_REG_USER_CTRL, &reg_user_ctrl);
    reg_user_ctrl &= ~0x20;
    mpu9250_drv_write_byte(MPU9250_REG_USER_CTRL, reg_user_ctrl);

    return true;
}

bool MPU9250_drv_start_lowpower_wom(void)
{
    mpu9250_drv_write_byte(MPU9250_REG_PWR_MGMT_1, 0x00);
    mpu9250_drv_write_byte(MPU9250_REG_PWR_MGMT_2, 0x07);   /* DIS_XG, DIS_YG, DIS_ZG */
    mpu9250_drv_write_byte(MPU9250_REG_ACCEL_CONFIG2, 0x09);/* ACCEL_FCHICE_B=1, A_DLPFCFG=1 */
    mpu9250_drv_write_byte(MPU9250_REG_INT_ENABLE, 0x40);
    mpu9250_drv_write_byte(MPU9250_REG_MOT_DETECT_CTRL, 0xc0);
    mpu9250_drv_write_byte(MPU9250_REG_LP_ACCEL_ODR, 0x06);
    mpu9250_drv_write_byte(MPU9250_REG_WOM_THR, 0x7f);
    mpu9250_drv_write_byte(MPU9250_REG_PWR_MGMT_1, 0x20);

    return true;
}

/*
 * Start maesure.
 */
bool MPU9250_drv_start_maesure(MPU9250_BIT_GYRO_FS_SEL gyro_fs, MPU9250_BIT_ACCEL_FS_SEL accel_fs, MPU9250_BIT_DLPF_CFG dlpf_cfg, MPU9250_BIT_A_DLPFCFG a_dlpfcfg)
{
    if (stat != MPU9250_STAT_IDLE) {
        return false;
    }
    /* Wakeup device */
    MPU9250_drv_wakeup();

    /* Start gyro */
    MPU9250_drv_start_gyro(gyro_fs, dlpf_cfg);
    /* Start accel */
    MPU9250_drv_start_accel(accel_fs, a_dlpfcfg);
    /* Start magnetometer */
    MPU9250_drv_start_magnetometer(AK8963_BIT_CNTL1_MODE_CNT1);

    stat = MPU9250_STAT_MAESUREING; /* Update STATE. */
    return true;
}

/*
 * Stop maesure.
 */
bool MPU9250_drv_stop_maesure(void)
{
    if (stat != MPU9250_STAT_MAESUREING) {
        return false;
    }

    mpu9250_drv_write_byte(MPU9250_REG_PWR_MGMT_2, 0x3f);   /* Disable Accel & Gyro */
    Usleep(1000);

    stat = MPU9250_STAT_IDLE;   /* Update STATE. */
    return true;
}

/*
 * Read Gyro.
 */
bool MPU9250_drv_read_gyro(MPU9250_gyro_val *gyro_val)
{
    uint8_t vals[6];

    if (stat != MPU9250_STAT_MAESUREING) {
        return false;
    }

    if (gyro_val == NULL) {
        return false;
    }

    for (int i = 0; i < sizeof(vals); i++) {
        mpu9250_drv_read_byte(MPU9250_REG_GYRO_XOUT_HL + i, &vals[i]);
    }

    gyro_val->raw_x = ((uint16_t)vals[0] << 8) | vals[1];
    gyro_val->raw_y = ((uint16_t)vals[2] << 8) | vals[3];
    gyro_val->raw_z = ((uint16_t)vals[4] << 8) | vals[5];

    gyro_val->x = (float)(int16_t)gyro_val->raw_x / gyro_div;
    gyro_val->y = (float)(int16_t)gyro_val->raw_y / gyro_div;
    gyro_val->z = (float)(int16_t)gyro_val->raw_z / gyro_div;

    return true;
}

/*
 * Read Accel.
 */
bool MPU9250_drv_read_accel(MPU9250_accel_val *accel_val)
{
    uint8_t vals[6];

    if (stat != MPU9250_STAT_MAESUREING) {
        return false;
    }

    if (accel_val == NULL) {
        return false;
    }

    for (int i = 0; i < 6; i++) {
        mpu9250_drv_read_byte(MPU9250_REG_ACCEL_XOUT_HL + i, &vals[i]);
    }

    accel_val->raw_x = ((uint16_t)vals[0] << 8) | vals[1];
    accel_val->raw_y = ((uint16_t)vals[2] << 8) | vals[3];
    accel_val->raw_z = ((uint16_t)vals[4] << 8) | vals[5];

    accel_val->x = (float)(int16_t)accel_val->raw_x / accel_div;
    accel_val->y = (float)(int16_t)accel_val->raw_y / accel_div;
    accel_val->z = (float)(int16_t)accel_val->raw_z / accel_div;

    return true;
}

/*
 * Read chip temperature.
 */
bool MPU9250_drv_read_temperature(MPU9250_temperature_val *temperature_val)
{
    uint8_t val[2];

    if (stat != MPU9250_STAT_MAESUREING) {
        return false;
    }

    if (temperature_val == NULL) {
        return false;
    }

    mpu9250_drv_read_byte(MPU9250_REG_TEMP_HL, &val[0]);
    mpu9250_drv_read_byte(MPU9250_REG_TEMP_HL + 1, &val[1]);

    temperature_val->raw = ((uint16_t)val[0] << 8) | val[1];

    return true;
}

/*
 * Read Magnetometer.
 */
bool MPU9250_drv_read_magnetometer(MPU9250_magnetometer_val *magnetometer_val)
{
    uint8_t vals[8];

    if (stat != MPU9250_STAT_MAESUREING) {
        return false;
    }

    if (magnetometer_val == NULL) {
        return false;
    }
    /* set read flag & slave address. */
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_ADDR, AK8963_I2C_ADDR | 0x80);
    /* set register address. */
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_REG, AK8963_REG_ST1);
    /* transfer */
    mpu9250_drv_write_byte(MPU9250_REG_I2C_SLV0_CTRL, 0x88);

    Usleep(1000);
    for (int i = 0; i < sizeof(vals); i++) {
        mpu9250_drv_read_byte(MPU9250_REG_EXT_SENS_DATA_00 + i, &vals[i]);
    }

    if ((vals[7] & 0x08) != 0) {
        //detect overflow
        return false;
    }
    /* RAW data */
    magnetometer_val->raw_x = ((uint16_t)vals[2] << 8) | vals[1];
    magnetometer_val->raw_y = ((uint16_t)vals[4] << 8) | vals[3];
    magnetometer_val->raw_z = ((uint16_t)vals[6] << 8) | vals[5];
    /* Real data */
    magnetometer_val->x = (int16_t)magnetometer_val->raw_x * ((((float)(int8_t)magnetometer_calib[0] - 128) / 256) + 1);
    magnetometer_val->y = (int16_t)magnetometer_val->raw_y * ((((float)(int8_t)magnetometer_calib[1] - 128) / 256) + 1);
    magnetometer_val->z = (int16_t)magnetometer_val->raw_z * ((((float)(int8_t)magnetometer_calib[2] - 128) / 256) + 1);

    return true;
}
