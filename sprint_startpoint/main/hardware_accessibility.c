#include "hardware_accessibility.h"

//initialization functions
//################################################################################################
void gpio_initialization() {	//init function configuring digital GPIOs & PWM outputs
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

void netif_ap_initialization() {
	esp_err_t ret;
	ret = nvs_flash_init();
	    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
	        ESP_ERROR_CHECK(nvs_flash_erase());
	        ret = nvs_flash_init();
	    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //following commands are based on ESP-IDF esp_netif_create_wifi_ap() function
    //difference: netif instance has to be initialized manually so that the IP subnet can be set to 192.168.5.* instead of 192.168.4.* as it is the standard
    //-> allows both the sprint measurement devices at the start AND end of the track to act as DHCP servers for different IP subnets
    //-> enables coexistence of two networks independently managed by two DHCP servers with the device at the end of the sprint track acting as the
    //   "man in the middle" for all communications comming from external devices accessing the management website
    //##########################################################################################################
	custom_esp_netif_inherent_config_t custom_netif_inherent_ap_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_AP();
    esp_netif_ip_info_t custom_ap_ip_info = {
            .ip = { .addr = ESP_IP4TOADDR( 192, 168, 5, 1) },
            .gw = { .addr = ESP_IP4TOADDR( 192, 168, 5, 1) },
            .netmask = { .addr = ESP_IP4TOADDR( 255, 255, 255, 0) },
    };
    custom_netif_inherent_ap_config.ip_info = &custom_ap_ip_info;
    esp_netif_config_t netif_cfg = {
		.base = &custom_netif_inherent_ap_config,
		.driver = NULL,
		.stack = _g_esp_netif_netstack_default_wifi_ap
    };

    esp_netif_t * instance = esp_netif_new(&netif_cfg);
    assert(instance);
    esp_netif_attach_wifi_ap(instance);
    esp_wifi_set_default_wifi_ap_handlers();
    //##########################################################################################################

	ret = esp_netif_set_hostname(instance, "sprintstart");
	ESP_ERROR_CHECK(ret);
	if (ret == ESP_ERR_ESP_NETIF_IF_NOT_READY) printf("interface status error");
	if (ret == ESP_ERR_ESP_NETIF_INVALID_PARAMS) printf("parameter error");
	return instance;
}

void wifi_initialization() {
	esp_err_t ret;

	wifi_init_config_t init_conf = WIFI_INIT_CONFIG_DEFAULT();
	wifi_config_t wifi_conf = {
		.ap = {
			.ssid = "ESP_LR",
			.password = "password",
			.ssid_len = 6,
			.channel = 0,
			.authmode = WIFI_AUTH_WPA2_PSK,
			.ssid_hidden = 0,
			.max_connection = 5,
			.beacon_interval = 100,
			.pairwise_cipher = WIFI_CIPHER_TYPE_TKIP_CCMP,
		},
	};

	if ((ret = esp_wifi_init(&init_conf)) != ESP_OK) {
		ESP_LOGE(WIFI_INIT_TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
		return;
	}

	if ((ret = esp_wifi_set_mode(WIFI_MODE_AP)) != ESP_OK) {
		ESP_LOGE(WIFI_INIT_TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
		return;
	}

	//SET PROTOCOLS ###########
	if ((ret = esp_wifi_set_protocol(ESP_IF_WIFI_AP, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N)) != ESP_OK) {
//	if ((ret = esp_wifi_set_protocol(ESP_IF_WIFI_AP, WIFI_PROTOCOL_LR)) != ESP_OK) {
		ESP_LOGE(WIFI_INIT_TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
		return;
	}
	//#########################

	if ((ret = esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_conf)) != ESP_OK) {
		ESP_LOGE(WIFI_INIT_TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
		return;
	}

	if ((ret = esp_wifi_start()) != ESP_OK) {
		ESP_LOGE(WIFI_INIT_TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
		return;
	}
}

void timer_initialization() { //init function to characterize and start the measurement timer
	timer_config_t timer_cfg = {
		.alarm_en = TIMER_ALARM_DIS,
		.counter_en = TIMER_PAUSE,
		.intr_type = TIMER_INTR_LEVEL,
		.counter_dir = TIMER_COUNT_UP,
		.auto_reload = TIMER_AUTORELOAD_DIS,
		.divider = TIMER_DIVIDER,
	};

	timer_init(TIMER_GROUP_1, TIMER_1, &timer_cfg);
	timer_set_counter_value(TIMER_GROUP_1, TIMER_1, (TIMER_COUNTSTART * TIMER_SCALE));
	timer_start(TIMER_GROUP_1, TIMER_1);
}

void adc_initialization() {	//init function to set up the analog digital conversion
	adc1_config_width(ADC_WIDTH_BIT_12);
	adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN_DB_11);
}

//################################################################################################

//utilization functions
//################################################################################################
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

void buzzerOn(uint8_t tone) { //play tone: 0 = low (3 kHz), 1 = medium (4 kHz), 2 = high (5 kHz)
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
	ledc_stop(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 0);	//stop buzzer beeping& setting buzzer pin idle level to 0 afterwards
}

void blueOn() {
	ledc_stop(LEDC_HIGH_SPEED_MODE, BLUE_LED_CHAN, 1);	//start blueLED by setting blueLED pin idle level to 1 after stopping PWM
}

void blueOff() {
	ledc_stop(LEDC_HIGH_SPEED_MODE, BLUE_LED_CHAN, 0);	//stop blueLED by setting blueLED pin idle level to 0 after stopping PWM
}

void blueBlinking() {	//start blueLED blinking by configuring PWM
	ledc_timer_rst(LEDC_HIGH_SPEED_MODE, BLUE_LED_TIMER);
	ledc_set_duty(LEDC_HIGH_SPEED_MODE, BLUE_LED_CHAN, BLUE_LED_DUTY);
	ledc_update_duty(LEDC_HIGH_SPEED_MODE, BLUE_LED_CHAN);
}

void greenOn() {
	ledc_stop(LEDC_HIGH_SPEED_MODE, GREEN_LED_CHAN, 1);	//start greenLED by setting greenLED pin idle level to 1 after stopping PWM
}

void greenOff() {
	ledc_stop(LEDC_HIGH_SPEED_MODE, GREEN_LED_CHAN, 0);	//stop greenLED by setting greenLED pin idle level to 0 after stopping PWM
}

void greenBlinking() {	//start blueLED blinking by configuring PWM
	ledc_timer_rst(LEDC_HIGH_SPEED_MODE, GREEN_LED_TIMER);
	ledc_set_duty(LEDC_HIGH_SPEED_MODE, GREEN_LED_CHAN, GREEN_LED_DUTY);
	ledc_update_duty(LEDC_HIGH_SPEED_MODE, GREEN_LED_CHAN);
}
//################################################################################################
