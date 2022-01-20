#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/spi_common.h"
#include "driver/timer.h"
#include "driver/ledc.h"
#include <dirent.h>
#include "driver/adc.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#define GPIO_INIT_TAG       "GPIO_INIT"
#define SD_INIT_TAG         "SD_INIT"
#define NETIF_AP_INIT_TAG   "NETIF_AP_INIT"
#define NETIF_STA_INIT_TAG  "NETIF_STA_INIT"
#define WIFI_INIT_TAG       "WIFI_INIT"
#define TIMER_INIT_TAG      "TIMER_INIT"
#define ADC_INIT_TAG        "ADC_INIT"

#define LASER_PIN			  GPIO_NUM_33 //LASER                                                                                   TOGGLE
#define RED_LED_PIN			GPIO_NUM_27 //LASER indicator: ON if LASER detected, OFF if LASER not detected                        TOGGLE

#define BUZZER_PIN			GPIO_NUM_32	//Audio (Buzzer)	indicator: various signals (Eg. synchronization, start of measurement)  PWM
#define BLUE_LED_PIN		GPIO_NUM_25	//WIFI connection	indicator: ON if connected, BLINKING if disconnected                    PWM
#define GREEN_LED_PIN		GPIO_NUM_26	//synchronization	indicator: OFF if no sync attempted, blinking if synced                 PWM

#define BUZZER_TIMER		  LEDC_TIMER_0
#define BLUE_LED_TIMER		LEDC_TIMER_1
#define GREEN_LED_TIMER		LEDC_TIMER_2

#define BUZZER_CHAN			LEDC_CHANNEL_0
#define BLUE_LED_CHAN		LEDC_CHANNEL_1
#define GREEN_LED_CHAN	LEDC_CHANNEL_2

#define BUZZER_DUTY_RES		LEDC_TIMER_1_BIT
#define LED_DUTY_RES		  LEDC_TIMER_20_BIT

#define LED_DUTY_MAX		1048576				//2^20 == 2^LEDC_TIMER_20_BIT
#define BLUE_LED_DUTY		(LED_DUTY_MAX / 2)
#define GREEN_LED_DUTY	(LED_DUTY_MAX / 8)

#define BUZZER_NORM_FREQ	4000
#define BLUE_LED_FREQ		2
#define GREEN_LED_FREQ		1

#define SD_MODE_MMC 0
#define SD_MODE_SPI 1

#define TIMER_DIVIDER		40000  //  Hardware timer clock divider
#define TIMER_SCALE			(TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define TIMER_COUNTSTART	0


#define ADC_CHANNEL			ADC1_CHANNEL_6


void gpio_initialization();
esp_err_t sd_initialization(uint8_t sd_mode);
void netif_ap_initialization();
void netif_sta_initialization();
void wifi_initialization();
void timer_initialization();
void adc_initialization();

void laserOn();
void laserOff();
void redOn();
void redOff();
void buzzerOn(uint8_t tone);
void buzzerOff();
void blueOn();
void blueOff();
void blueBlinking();
void greenOn();
void greenOff();
void greenBlinking();
