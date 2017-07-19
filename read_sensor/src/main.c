/**
 * @file   main.c
 * @brief  Application main.
 *
 * @author Cerevo Inc.
 */

/*
Copyright 2015 Cerevo Inc.

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
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "TZ01_system.h"
#include "TZ01_console.h"
#include "TZ01_motion_tracker.h"

#include "utils.h"

int main(void)
{
	volatile uint32_t i;
    /* Initialize */
    TZ01_system_init();
    TZ01_console_init();
    TZ01_motion_tracker_init();

    for (;;) {
    	char buf[32],result[32];
    	uint16_t x, y, z;
    	float dx, dy, dz;//ダミー
    	//加速度について
    	TZ01_motion_tracker_accel_read(&x, &y, &z, &dx, &dy, &dz);
    	sprintf(result, "%04x,%04x,%04x,", x, y, z);
    	
    	//角加速度について
    	TZ01_motion_tracker_gyro_read(&x, &y, &z, &dx, &dy, &dz);
    	sprintf(buf, "%04x,%04x,%04x\r", x, y, z);
    	strcat(result,buf);//O(n)無駄になるけど見やすさのためにわざとする
    	
    	//地磁気について
    	/*
    	TZ01_motion_tracker_magnetometer_read(&x, &y, &z, &dx, &dy, &dz);
    	sprintf(buf, "%f,%f,%f\r", dx, dy, dz);
    	strcat(result,buf);
  		*/
    	//送信
    	
    	TZ01_console_puts(result);
    	
    	//待機
    	for(i=0;i<0x7ffff;i++);
    	
    	
        if (TZ01_system_run() == RUNEVT_POWOFF) {
            /* Power off operation detected */
            break;
        }
        
    }

    TZ01_console_puts("Program terminated.\r\n");
    return 0;
}
