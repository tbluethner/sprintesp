#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_netif_types.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h> //for boolean data type (for some reason...)

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/timer.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

#include "esp_tls_crypto.h"
#include "esp_http_server.h"
#include "esp_http_client.h"

#include <math.h> //for pow() function

#include "pinout_utils.h" //custom library with GPIO toggle & PWM definitions & functions

//IMPORTANT: CDU 1 Idle task watchdog DISABLED in menuconfig -> Component config -> Common ESP-related -> Watch CPU1 Idle Task!

#define TAG "SPRINTSTART"

#define TIMER_DIVIDER		40000  //  Hardware timer clock divider
#define TIMER_SCALE			(TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define TIMER_COUNTSTART	0
#define MASTER_URL			"http://192.168.4.1/"

#define VOLTAGE_LIMIT 		400		// lower limit for laser receiver diode: 400 mV

#define ADC_CHANNEL			ADC1_CHANNEL_6

//globale (funktionsübergreifende) Variablen
double starttime_system;
double current_time = 0;
uint8_t in_sync_mode = 0;
uint8_t in_sprint_mode = 0;
uint8_t in_align_mode = 0;

char laser_intr_time[30] = ""; //string with timecount (sec)

int charToInt(char * array) {
	int array_len = strlen(array);
	int result = 0;
	for (int i = 0; i < array_len; i++) {
		result += (array[i] - '0') * (pow(10, (array_len - (i+1))));
	}

	return result;
}

void lightBarrierTask(char * time_str) {
	in_sprint_mode = 1;
	laserOn();
	ESP_LOGI(TAG, "lightBarrierTask started!");
	double current_time = 0;
    esp_adc_cal_characteristics_t *adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1092, adc_chars); //1092 mV: Referenzspannung Vref, geroutet auf z.B. GPIO25 mit adc2_vref_to_gpio(GPIO_NUM_25)	=> nur mgl. mit ADC2-Pins

	if (esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC_CHANNEL), adc_chars) < VOLTAGE_LIMIT) {
		strcpy(time_str, "nc");
		ESP_LOGE(TAG, "Optical barrier not in line for measurement!");
	} else {
		strcpy(time_str, "pending");
		redOn();
		while (in_sprint_mode == 1) {
	//		snprintf(voltage_string, sizeof(voltage_string), "%d", adc1_get_raw(ADC_CHANNEL));
			if (esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC_CHANNEL), adc_chars) < VOLTAGE_LIMIT) {
				timer_get_counter_time_sec(TIMER_GROUP_1, TIMER_1, &current_time);
				sprintf(time_str, "%.3f", current_time);
				ESP_LOGI(TAG, "Optical barrier interrupted at %s! seconds!", time_str);
				break;	//break out of loop to vTaskDelete()
			}
			vTaskDelay(0.1 / portTICK_PERIOD_MS);
		}
	}
	laserOff();
	redOff();
	in_sprint_mode = 0;
	vTaskDelete(NULL);
}

void alignIndicatorTask() {
	in_align_mode = 1;
	ESP_LOGI(TAG, "alignIndicatorTask started!");
    esp_adc_cal_characteristics_t *adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1092, adc_chars); //1092 mV: Referenzspannung Vref, geroutet auf z.B. GPIO25 mit adc2_vref_to_gpio(GPIO_NUM_25)	=> nur mgl. mit ADC2-Pins

    redOff();
    uint8_t red_level = 0;

	while (in_align_mode == 1) {
		if (esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC_CHANNEL), adc_chars) > VOLTAGE_LIMIT) {
			if (red_level == 0) {
				redOn();
				red_level = 1;
			}
		} else {
			if (red_level == 1) {
				redOff();
				red_level = 0;
			}
		}
		vTaskDelay(50 / portTICK_PERIOD_MS); //no need for exact time accuracy, so bigger delay is acceptable
	}
	redOff();
	in_align_mode = 0;
	vTaskDelete(NULL);
}

void syncTimerTask() {
	in_sync_mode = 1;
	ESP_LOGI(TAG, "syncTimerTask started!");
	double start_time;
	timer_get_counter_time_sec(TIMER_GROUP_1, TIMER_1, &start_time);
	double current_time = start_time;
    esp_adc_cal_characteristics_t *adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1092, adc_chars); //1092 mV: Referenzspannung Vref, geroutet auf z.B. GPIO25 mit adc2_vref_to_gpio(GPIO_NUM_25)	=> nur mgl. mit ADC2-Pins
    int voltage;
	while (1) {
		voltage = esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC_CHANNEL), adc_chars);
		if (voltage > VOLTAGE_LIMIT) {
			timer_set_counter_value(TIMER_GROUP_1, TIMER_1, (TIMER_COUNTSTART * TIMER_SCALE));
			printf("raw: %d\n", voltage);
			printf("lim: %d\n", VOLTAGE_LIMIT);
		    buzzerOn(1);
		    greenOn();
		    vTaskDelay(500 / portTICK_PERIOD_MS);
		    buzzerOff();
		    greenOff();
		    vTaskDelay(500 / portTICK_PERIOD_MS);

		    buzzerOn(1);
		    greenOn();
		    vTaskDelay(100 / portTICK_PERIOD_MS);
		    buzzerOff();
		    greenOff();
		    vTaskDelay(100 / portTICK_PERIOD_MS);
		    buzzerOn(1);
		    greenOn();
		    vTaskDelay(100 / portTICK_PERIOD_MS);
		    buzzerOff();
		    greenOff();
		    vTaskDelay(100 / portTICK_PERIOD_MS);
		    buzzerOn(1);
		    greenOn();
		    vTaskDelay(100 / portTICK_PERIOD_MS);
		    buzzerOff();
		    greenOff();
		    vTaskDelay(100 / portTICK_PERIOD_MS);

		    buzzerOn(2);
		    greenOn();
		    vTaskDelay(100 / portTICK_PERIOD_MS);
		    buzzerOff();
		    greenOff();
		    vTaskDelay(100 / portTICK_PERIOD_MS);
		    greenBlinking();
			ESP_LOGI(TAG, "Timer synced / reset!");
			break;
		}
		if ((current_time - start_time) > 5) {
			ESP_LOGE(TAG, "Timer sync timeout! Abort!");
			break;
		}
		timer_get_counter_time_sec(TIMER_GROUP_1, TIMER_1, &current_time);
		vTaskDelay(0.1 / portTICK_PERIOD_MS);
	}
	in_sync_mode = 0;
	vTaskDelete(NULL);
}

TaskHandle_t startLightBarrier() {
	BaseType_t taskCreateReturn = NULL;
	TaskHandle_t taskVoltageHandle = NULL;

	if (in_sprint_mode == 1) {
		taskCreateReturn = xTaskCreatePinnedToCore(
			lightBarrierTask,			//task function
			"lightBarrierTask",			//task name
			5000,						//stack size
			laser_intr_time,			//task parameters -> here: timestring (global var)
			configMAX_PRIORITIES - 1,	//task priority
			&taskVoltageHandle,			//task handle
			1							//task CPU core
		);
	}
    if(taskCreateReturn == pdPASS) {
        ESP_LOGI(TAG, "lightBarrierTask created successfully!");
    } else if (in_sprint_mode != 1) {
    	ESP_LOGI(TAG, "lightBarrierTask start canceled successfully!");
    } else {
        ESP_LOGE(TAG, "lightBarrierTask NOT created successfully!");
    }
    return taskVoltageHandle;
}

TaskHandle_t startAlignIndicator() {
	BaseType_t taskCreateReturn = NULL;
	TaskHandle_t taskAlignHandle = NULL;
	taskCreateReturn = xTaskCreatePinnedToCore(
		alignIndicatorTask,			//task function
		"alignIndicatorTask",		//task name
		5000,						//stack size
		NULL,			//task parameters -> here: timestring (global var)
		configMAX_PRIORITIES - 1,	//task priority
		&taskAlignHandle,			//task handle
		1							//task CPU core
	);
    if(taskCreateReturn == pdPASS) {
        ESP_LOGI(TAG, "alignIndicatorTask created successfully!");
    } else {
        ESP_LOGE(TAG, "alignIndicatorTask NOT created successfully!");
    }
    return taskAlignHandle;
}

TaskHandle_t initSyncTimerTask() {
	BaseType_t taskCreateReturn;
	TaskHandle_t taskSyncTimerHandle = NULL;

	taskCreateReturn = xTaskCreatePinnedToCore(
		syncTimerTask,				//task function
		"syncTimerTask",			//task name
		5000,						//stack size
		NULL,						//task parameters
		configMAX_PRIORITIES - 1,	//task priority
		&taskSyncTimerHandle,			//task handle
		1							//task CPU core
	);
    if(taskCreateReturn == pdPASS) {
        ESP_LOGI(TAG, "syncTimerTask created successfully!");
    } else {
        ESP_LOGE(TAG, "syncTimerTask NOT created successfully!");
    }
    return taskSyncTimerHandle;
}

void CountdownBuzzerTask() {
	ESP_LOGI(TAG, "buzzerTask started!");
    buzzerOn(0);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    buzzerOff();
    vTaskDelay(500 / portTICK_PERIOD_MS);

    buzzerOn(0);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    buzzerOff();
    vTaskDelay(500 / portTICK_PERIOD_MS);

    buzzerOn(1);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    buzzerOff();
    startLightBarrier();
	vTaskDelete(NULL);
}


TaskHandle_t startCountdownBuzzer() {
	BaseType_t taskCreateReturn = NULL;
	TaskHandle_t taskBuzzerHandle = NULL;

	if (in_sprint_mode == 1) {
		taskCreateReturn = xTaskCreatePinnedToCore(
			CountdownBuzzerTask,		//task function
			"CountdownBuzzerTask",		//task name
			5000,						//stack size
			NULL,						//task parameters
			configMAX_PRIORITIES - 1,	//task priority
			&taskBuzzerHandle,			//task handle
			1							//task CPU core
		);
	}
    return taskBuzzerHandle;
}

void IRAM_ATTR timer_interrupt_handler (void *HDF) {
	timer_disable_intr(TIMER_GROUP_1, TIMER_1);
	timer_group_clr_intr_status_in_isr(TIMER_GROUP_1, TIMER_1);
	timer_set_alarm(TIMER_GROUP_1, TIMER_1, TIMER_ALARM_DIS);
	startCountdownBuzzer();
}

esp_err_t uri_get(httpd_req_t* req)
{
    size_t buf_len = httpd_req_get_url_query_len(req) + 1; // +1 for NULL termination
    char signal_param[50];

	if (buf_len > 1) {
		char * buf = malloc(buf_len);
		if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
			if (httpd_query_key_value(buf, "signal", signal_param, sizeof(signal_param)) == ESP_OK) {
				if (strcmp(signal_param, "getStartTimecount") == 0) {           //0: exact match
					double current_time = 0;
					char time_str[30];
					timer_get_counter_time_sec(TIMER_GROUP_1, TIMER_1, &current_time);
					sprintf(time_str, "%.3f", current_time);
					ESP_LOGI(TAG, "Requested Start Device Timestring: %s\n", time_str);
					httpd_resp_sendstr(req, time_str);
				}
				if (strcmp(signal_param, "getStartLastLaserIntr") == 0) {           //0: exact match
					ESP_LOGI(TAG, "Last Start Laser Intr Timestring: %s\n", laser_intr_time);
					httpd_resp_sendstr(req, laser_intr_time);
				}
				free(buf);
				return ESP_OK;
			}
		}
		free(buf);
	}
	httpd_resp_send(req, "", 0);
	return ESP_OK;
}

esp_err_t uri_post(httpd_req_t* req)
{
    size_t buf_len = httpd_req_get_url_query_len(req) + 1; // +1 for NULL termination
    char signal_param[50];

	if (buf_len > 1) {
		char * buf = malloc(buf_len);
		if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
			if (httpd_query_key_value(buf, "signal", signal_param, sizeof(signal_param)) == ESP_OK) {
				if (strcmp(signal_param, "startSprint") == 0) {           //0: exact match
					size_t seconds_len = httpd_req_get_hdr_value_len(req, "seconds") + 1; //header contains the second mark that the synced devices have to pass so they start their buzzer
				    if (seconds_len > 1) {
				        char * seconds_buf = malloc(seconds_len);
				        if (httpd_req_get_hdr_value_str(req, "seconds", seconds_buf, seconds_len) == ESP_OK) {
				        	int seconds_int = charToInt(seconds_buf);
				        	if (in_sprint_mode == 0) {
								timer_set_alarm_value(TIMER_GROUP_1, TIMER_1, (seconds_int * TIMER_SCALE));
								timer_set_alarm(TIMER_GROUP_1, TIMER_1, TIMER_ALARM_EN);
								timer_enable_intr(TIMER_GROUP_1, TIMER_1);
								in_sprint_mode = 1;
								httpd_resp_sendstr(req, "started");
							} else {
								httpd_resp_sendstr(req, "underway");
							}
				        }
				        free(seconds_buf);
				    } else {
				    	ESP_LOGE(TAG, "Header \"seconds\" not found! Sprint measurement not started!");
				    }
				}
				if (strcmp(signal_param, "stopSprint") == 0) {           //0: exact match
					in_sprint_mode = 0;
					httpd_resp_sendstr(req, "stopped");
				}
				if (strcmp(signal_param, "startAlign") == 0) {           //0: exact match
				    laserOn();
					if (in_align_mode == 0) {
						startAlignIndicator();
						httpd_resp_sendstr(req, "started");
					} else {
						httpd_resp_sendstr(req, "pending");
					}
				}
				if (strcmp(signal_param, "stopAlign") == 0) {           //0: exact match
					laserOff();
					in_align_mode = 0;
					httpd_resp_sendstr(req, "stopped");
				}
				if (strcmp(signal_param, "startTimeSync") == 0) {           //0: exact match
					if (in_sync_mode == 0) {
						initSyncTimerTask();
						httpd_resp_sendstr(req, "started");
					} else {
						httpd_resp_sendstr(req, "underway");
					}
				}
				free(buf);
				return ESP_OK;
			}
		}
		free(buf);
	}
	httpd_resp_send(req, "", 0);
	return ESP_OK;
}


httpd_uri_t uri_get_struct = {
    .uri      = "/uri_get",
    .method   = HTTP_GET,
    .handler  = uri_get,
    .user_ctx = NULL
};

httpd_uri_t uri_post_struct = {
    .uri      = "/uri_post",
    .method   = HTTP_POST,
    .handler  = uri_post,
    .user_ctx = NULL
};

httpd_handle_t httpserver_initialization() {
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	httpd_handle_t server = NULL;
	if (httpd_start(&server, &config) == ESP_OK) {
		httpd_register_uri_handler(server, &uri_get_struct);
		httpd_register_uri_handler(server, &uri_post_struct);
	}
	return server;
}


esp_http_client_handle_t init_http_request(int method, char * signal, char * post_field, char headers[][2][50], int header_count) {
    esp_http_client_config_t client_cfg = {
        .url = MASTER_URL,
    };
    esp_http_client_handle_t client = esp_http_client_init(&client_cfg);
    char url_buf[100];
    esp_http_client_get_url(client, url_buf, 100);
    switch (method) {
    	case HTTP_METHOD_GET:
    		strcat(url_buf, "uri_get");
    	case HTTP_METHOD_POST:
    		strcat(url_buf, "uri_post");
    }
    if (strcmp(signal, "") != 0 && signal != NULL) {
    	strcat(url_buf, "?signal=");
    	strcat(url_buf, signal);
    }
	esp_http_client_set_url(client, url_buf);
    esp_http_client_get_url(client, url_buf, 100);
	ESP_LOGI(TAG, "HTTP request URL: %s", url_buf);
    if (strcmp(post_field, "") != 0 && post_field != NULL) {
    	esp_http_client_set_post_field(client, post_field, strlen(post_field));
    }


	esp_http_client_set_method(client, method);

	int i = 0;
	while (i < header_count) {
		esp_http_client_set_header(client, headers[i][0], headers[i][1]);
		ESP_LOGI(TAG, "HTTP request header set: %s: %s", headers[i][0], headers[i][1]);
		i++;
	}

	esp_err_t err = esp_http_client_perform(client);
	if (err == ESP_OK) {
		ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
				esp_http_client_get_status_code(client),
				esp_http_client_get_content_length(client));
	} else {
		ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
	}
	return client;
}

esp_err_t get_sprint_timer(char * out_buf, int len_max) {
	esp_http_client_handle_t client = init_http_request(HTTP_METHOD_GET, "getStartTimecount", NULL, NULL, 0);
	char data_buf[30];
	esp_http_client_read_response(client, data_buf, 30);
	esp_err_t err = esp_http_client_cleanup(client);
	strncpy(out_buf, data_buf, len_max);
	return err;
}

typedef struct custom_esp_netif_inherent_config {
    esp_netif_flags_t flags;         /*!< flags that define esp-netif behavior */
    uint8_t mac[6];                  /*!< initial mac address for this interface */
    esp_netif_ip_info_t* ip_info;    /*!< initial ip address for this interface */
    uint32_t get_ip_event;           /*!< event id to be raised when interface gets an IP */
    uint32_t lost_ip_event;          /*!< event id to be raised when interface losts its IP */
    const char * if_key;             /*!< string identifier of the interface */
    const char * if_desc;            /*!< textual description of the interface */
    int route_prio;                  /*!< numeric priority of this interface to become a default
                                          routing if (if other netifs are up).
                                          A higher value of route_prio indicates
                                          a higher priority */
} custom_esp_netif_inherent_config_t;

esp_netif_t* esp_netif_create_custom_wifi_ap() {
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

    esp_netif_t *netif = esp_netif_new(&netif_cfg);
    assert(netif);
    esp_netif_attach_wifi_ap(netif);
    esp_wifi_set_default_wifi_ap_handlers();
    return netif;
}

esp_netif_t * netif_initialization() {
	esp_err_t ret;
	ret = nvs_flash_init();
	    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
	        ESP_ERROR_CHECK(nvs_flash_erase());
	        ret = nvs_flash_init();
	    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t * instance = esp_netif_create_custom_wifi_ap();

	ret = esp_netif_set_hostname(instance, "sprintstart");
	ESP_ERROR_CHECK(ret);
	if (ret == ESP_ERR_ESP_NETIF_IF_NOT_READY) printf("interface status error");
	if (ret == ESP_ERR_ESP_NETIF_INVALID_PARAMS) printf("parameter error");
//	esp_netif_dhcps_stop(instance);
//	esp_netif_dhcpc_start(instance);
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
		ESP_LOGE(TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
		return;
	}

	if ((ret = esp_wifi_set_mode(WIFI_MODE_AP)) != ESP_OK) {
		ESP_LOGE(TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
		return;
	}

	if ((ret = esp_wifi_set_protocol(ESP_IF_WIFI_AP, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N)) != ESP_OK) {
//	if ((ret = esp_wifi_set_protocol(ESP_IF_WIFI_AP, WIFI_PROTOCOL_LR)) != ESP_OK) {
		ESP_LOGE(TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
		return;
	}

	if ((ret = esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_conf)) != ESP_OK) {
		ESP_LOGE(TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
		return;
	}

	if ((ret = esp_wifi_start()) != ESP_OK) {
		ESP_LOGE(TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
		return;
	}
}

void timer_initialization() {
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
	timer_isr_register(TIMER_GROUP_1, TIMER_1, &timer_interrupt_handler, NULL, ESP_INTR_FLAG_IRAM, NULL);	//Interrupt-Funktion wird registriert

}

void adc_initialization() {
	adc1_config_width(ADC_WIDTH_BIT_12);
	adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN_DB_11);
}

void main_c0(void)
{
	gpio_initialization();
	adc_initialization();
	timer_initialization();
	netif_initialization();
	wifi_initialization();
	httpserver_initialization();

    vTaskDelete(NULL);
}

void app_main(void) {
	BaseType_t taskCreateReturn = NULL;
	TaskHandle_t taskBuzzerHandle = NULL;

	taskCreateReturn = xTaskCreatePinnedToCore(
		main_c0,					//task function
		"maintask_core0",			//task name
		3584,						//stack size
		NULL,						//task parameters
		18,	//task priority
		&taskBuzzerHandle,			//task handle
		0							//task CPU core
	);
}
