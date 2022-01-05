#include "driver/gpio.h"
#include "driver/ledc.h"

#include "pinout_utils.h"

void gpio_initialization() {
	gpio_set_direction(LASER_PIN, GPIO_MODE_OUTPUT);
	gpio_set_level(LASER_PIN, 0);
	gpio_set_direction(RED_LED_PIN, GPIO_MODE_OUTPUT);
	gpio_set_level(RED_LED_PIN, 0);

	ledc_timer_config_t pwm_cfg_buzzer = {
		.speed_mode = LEDC_HIGH_SPEED_MODE,
		.duty_resolution = BUZZER_DUTY_RES,
		.timer_num = BUZZER_TIMER,
		.freq_hz = BUZZER_NORM_FREQ,
		.clk_cfg = LEDC_AUTO_CLK,
	};
	ledc_timer_config(&pwm_cfg_buzzer);

	ledc_channel_config_t led_channel_cfg_buzzer = {
		.gpio_num = BUZZER_PIN,
		.speed_mode = LEDC_HIGH_SPEED_MODE,
		.channel = BUZZER_CHAN,
		.intr_type = LEDC_INTR_DISABLE,
		.timer_sel = BUZZER_TIMER,
		.duty = 0,
		.hpoint = 0,
	};
	ledc_channel_config(&led_channel_cfg_buzzer);

	ledc_timer_config_t pwm_cfg_blue = {
		.speed_mode = LEDC_HIGH_SPEED_MODE,
		.duty_resolution = LED_DUTY_RES,
		.timer_num = BLUE_LED_TIMER,
		.freq_hz = BLUE_LED_FREQ,
		.clk_cfg = LEDC_AUTO_CLK,
	};
	ledc_timer_config(&pwm_cfg_blue);

	ledc_channel_config_t led_channel_cfg_blue = {
		.gpio_num = BLUE_LED_PIN,
		.speed_mode = LEDC_HIGH_SPEED_MODE,
		.channel = BLUE_LED_CHAN,
		.intr_type = LEDC_INTR_DISABLE,
		.timer_sel = BLUE_LED_TIMER,
		.duty = 0,
		.hpoint = 0,
	};
	ledc_channel_config(&led_channel_cfg_blue);

	ledc_timer_config_t pwm_cfg_green = {
		.speed_mode = LEDC_HIGH_SPEED_MODE,
		.duty_resolution = LED_DUTY_RES,
		.timer_num = GREEN_LED_TIMER,
		.freq_hz = GREEN_LED_FREQ,
		.clk_cfg = LEDC_AUTO_CLK,
	};
	ledc_timer_config(&pwm_cfg_green);

	ledc_channel_config_t led_channel_cfg_green = {
		.gpio_num = GREEN_LED_PIN,
		.speed_mode = LEDC_HIGH_SPEED_MODE,
		.channel = GREEN_LED_CHAN,
		.intr_type = LEDC_INTR_DISABLE,
		.timer_sel = GREEN_LED_TIMER,
		.duty = 0,
		.hpoint = 0,
	};
	ledc_channel_config(&led_channel_cfg_green);
}

void laserOn() {
	gpio_set_level(LASER_PIN, 1);
}

void laserOff() {
	gpio_set_level(LASER_PIN, 0);
}

void redOn() {
	gpio_set_level(RED_LED_PIN, 1);
}

void redOff() {
	gpio_set_level(RED_LED_PIN, 0);
}

void buzzerOn(uint8_t tone) { //tone / frequency: 0 = low, 1 = medium, 2 = high
	ledc_timer_rst(LEDC_HIGH_SPEED_MODE, BUZZER_TIMER);
	if (tone == 0) {
		ledc_set_freq(LEDC_HIGH_SPEED_MODE, BUZZER_TIMER, BUZZER_NORM_FREQ - 1000);
	} else if (tone == 1) {
		ledc_set_freq(LEDC_HIGH_SPEED_MODE, BUZZER_TIMER, BUZZER_NORM_FREQ);
	} else if (tone == 2) {
		ledc_set_freq(LEDC_HIGH_SPEED_MODE, BUZZER_TIMER, BUZZER_NORM_FREQ + 1000);
	}

	ledc_set_duty(LEDC_HIGH_SPEED_MODE, BUZZER_CHAN, 1);
	ledc_update_duty(LEDC_HIGH_SPEED_MODE, BUZZER_CHAN);
}

void buzzerOff() {
	ledc_stop(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 0);
}

void blueOn() {
	ledc_stop(LEDC_HIGH_SPEED_MODE, BLUE_LED_CHAN, 1);
}

void blueOff() {
	ledc_stop(LEDC_HIGH_SPEED_MODE, BLUE_LED_CHAN, 0);
}

void blueBlinking() {
	ledc_timer_rst(LEDC_HIGH_SPEED_MODE, BLUE_LED_TIMER);
	ledc_set_duty(LEDC_HIGH_SPEED_MODE, BLUE_LED_CHAN, BLUE_LED_DUTY);
	ledc_update_duty(LEDC_HIGH_SPEED_MODE, BLUE_LED_CHAN);
}

void greenOn() {
	ledc_stop(LEDC_HIGH_SPEED_MODE, GREEN_LED_CHAN, 1);
}

void greenOff() {
	ledc_stop(LEDC_HIGH_SPEED_MODE, GREEN_LED_CHAN, 0);
}

void greenBlinking() {
	ledc_timer_rst(LEDC_HIGH_SPEED_MODE, GREEN_LED_TIMER);
	ledc_set_duty(LEDC_HIGH_SPEED_MODE, GREEN_LED_CHAN, GREEN_LED_DUTY);
	ledc_update_duty(LEDC_HIGH_SPEED_MODE, GREEN_LED_CHAN);
}
