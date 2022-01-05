#include "driver/gpio.h"

#define LASER_PIN			GPIO_NUM_33	//LASER																						TOGGLE
#define RED_LED_PIN			GPIO_NUM_27 //LASER				indicator: ON if LASER detected, OFF if LASER not detected				TOGGLE

#define BUZZER_PIN			GPIO_NUM_32	//Audio (Buzzer)	indicator: various signals (Eg. synchronization, start of measurement)	PWM
#define BLUE_LED_PIN		GPIO_NUM_25	//WIFI connection	indicator: ON if connected, BLINKING if disconnected					PWM
#define GREEN_LED_PIN		GPIO_NUM_26	//synchronization	indicator: OFF if no sync attempted, blinking if synced					PWM

#define BUZZER_TIMER		LEDC_TIMER_0
#define BLUE_LED_TIMER		LEDC_TIMER_1
#define GREEN_LED_TIMER		LEDC_TIMER_2

#define BUZZER_CHAN			LEDC_CHANNEL_0
#define BLUE_LED_CHAN		LEDC_CHANNEL_1
#define GREEN_LED_CHAN		LEDC_CHANNEL_2

#define BUZZER_DUTY_RES		LEDC_TIMER_1_BIT
#define LED_DUTY_RES		LEDC_TIMER_20_BIT

#define LED_DUTY_MAX		1048576				//2^20 == 2^LEDC_TIMER_20_BIT
#define BLUE_LED_DUTY		(LED_DUTY_MAX / 2)
#define GREEN_LED_DUTY		(LED_DUTY_MAX / 8)

#define BUZZER_NORM_FREQ	4000
#define BLUE_LED_FREQ		2
#define GREEN_LED_FREQ		1

void gpio_initialization();
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
