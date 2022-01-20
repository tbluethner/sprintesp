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

esp_err_t sd_initialization(uint8_t sd_mode) {	//init function to access SD card
	esp_vfs_fat_mount_config_t mnt_cfg = {
		.format_if_mount_failed = 0,
		.max_files = 5,
		.allocation_unit_size = 0,
	};
	esp_err_t ret = ESP_FAIL;	//returned as Fail if no mounting has been tried
	if (sd_mode == SD_MODE_MMC) {
		sdmmc_host_t host = SDMMC_HOST_DEFAULT();
		sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
		ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mnt_cfg, 0);
	} else if (sd_mode == SD_MODE_SPI) {
		sdmmc_host_t host = SDSPI_HOST_DEFAULT();
		spi_bus_config_t spi_bus_conf = {
			.mosi_io_num = GPIO_NUM_23,
			.miso_io_num = GPIO_NUM_19,
			.sclk_io_num = GPIO_NUM_18,
			.quadwp_io_num = -1,
			.quadhd_io_num = -1,
			.max_transfer_sz = 0,
		};
		spi_bus_initialize(host.slot, &spi_bus_conf, SPI_DMA_CH_AUTO);
		sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
		slot_config.host_id = host.slot;
		slot_config.gpio_cs = GPIO_NUM_13;
		slot_config.gpio_cd = -1;
		ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mnt_cfg, NULL);
	}
	return ret;
}


void netif_ap_initialization() {	//init function for ESP-IDF network stack
	esp_err_t ret;
	ret = nvs_flash_init();
	    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
	        ESP_ERROR_CHECK(nvs_flash_erase());
	        ret = nvs_flash_init();
	    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_t * instance = esp_netif_create_default_wifi_ap();
	ret = esp_netif_set_hostname(instance, "sprintend_ap");
	ESP_ERROR_CHECK(ret);
	if (ret == ESP_ERR_ESP_NETIF_IF_NOT_READY) printf("interface status error");
	if (ret == ESP_ERR_ESP_NETIF_INVALID_PARAMS) printf("parameter error");
}
void netif_sta_initialization() {
	esp_err_t ret;
	esp_netif_t * instance = esp_netif_create_default_wifi_sta();
	ret = esp_netif_set_hostname(instance, "sprintend_sta");
	ESP_ERROR_CHECK(ret);
	if (ret == ESP_ERR_ESP_NETIF_IF_NOT_READY) printf("interface status error");
	if (ret == ESP_ERR_ESP_NETIF_INVALID_PARAMS) printf("parameter error");
}

void wifi_initialization() {
	esp_err_t ret;
		wifi_init_config_t init_conf = WIFI_INIT_CONFIG_DEFAULT();
		wifi_config_t ap_wifi_conf = {
			.ap = {
				.ssid = "SPRINTMEASUREMENT_ESP",
				.password = "password",
				.ssid_len = 21,
				.channel = 0,
				.authmode = WIFI_AUTH_WPA2_PSK,
				.ssid_hidden = 0,
				.max_connection = 5,
				.beacon_interval = 100,
				.pairwise_cipher = WIFI_CIPHER_TYPE_TKIP_CCMP,
			},
		};
		wifi_config_t sta_wifi_conf = {
			.sta = {
				.ssid = "ESP_LR",
				.password = "password",
				.scan_method = WIFI_FAST_SCAN,
				.bssid_set = 0,
			},
		};

		if ((ret = esp_wifi_init(&init_conf)) != ESP_OK) {
			ESP_LOGE(WIFI_INIT_TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
			return;
		}

		if ((ret = esp_wifi_set_mode(WIFI_MODE_APSTA)) != ESP_OK) {
			ESP_LOGE(WIFI_INIT_TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
			return;
		}
		//SET PROTOCOLS ###########
		if ((ret = esp_wifi_set_protocol(ESP_IF_WIFI_AP, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N)) != ESP_OK) {
			ESP_LOGE(WIFI_INIT_TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
			return;
		}
		if ((ret = esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N)) != ESP_OK) { //during later versions: plans to switch PROTOCOL between measurement devices to Espressif LongRange
			ESP_LOGE(WIFI_INIT_TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
			return;
		}
		//#########################

		if ((ret = esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_wifi_conf)) != ESP_OK) {
			ESP_LOGE(WIFI_INIT_TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
			return;
		}

		if ((ret = esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_wifi_conf)) != ESP_OK) {
			ESP_LOGE(WIFI_INIT_TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
			return;
		}

		if ((ret = esp_wifi_start()) != ESP_OK) {
			ESP_LOGE(WIFI_INIT_TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
			return;
		}


		if ((ret = esp_wifi_connect()) != ESP_OK) {
			ESP_LOGE(WIFI_INIT_TAG, "%s wifi connect failed: %s\n", __func__, esp_err_to_name(ret));
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
