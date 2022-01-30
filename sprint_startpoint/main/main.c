//IMPORTANT: CDU 1 Idle task watchdog DISABLED in menuconfig -> Component config -> Common ESP-related -> Watch CPU1 Idle Task!

#include "freertos/FreeRTOS.h"      //FreeRTOS = task scheduling system (also for using multiple CPU cores)
#include "esp_log.h"                //ESP LOG messages (over UART)
#include <stdio.h>
#include <string.h>
#include "esp_adc_cal.h"            //ESP analog digital conversion (precise)
#include "esp_http_server.h"        //ESP HTTP server
#include <math.h>                   //for pow() function

#include "hardware_accessibility.h" //custom library to utilize GPIO, PWM, ADC, Timer, WIFI

#define TAG "SPRINTSTART"           //TAG printed before all UART log messages
#define VOLTAGE_LIMIT 		400		//lower limit for laser receiver diode: 400 mV

uint8_t in_sync_mode = 0;           //flag indicating the optical timer synchronization process being active
uint8_t in_sprint_mode = 0;         //flag indicating the sprint measurement process being active
uint8_t in_align_mode = 0;          //flag indicating the laser alignment process being active

char laser_intr_time[30] = "";      //string containing the time (seconds) the light barrier has been interrupted last

int charToInt(char * array) {       //helper function converting string to integer
	int array_len = strlen(array);
	int result = 0;
	for (int i = 0; i < array_len; i++) {
		result += (array[i] - '0') * (pow(10, (array_len - (i+1))));
	}

	return result;
}

//HTTP_GET-related functions
//#######################################################################################################################################################################
esp_err_t uri_get(httpd_req_t* req) {    //helper function to process incoming HTTP GET requests
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
//#######################################################################################################################################################################

//HTTP_POST-related functions
//#######################################################################################################################################################################
void lightBarrierTask(char * time_str) {    //function repeatedly checking whether the optical detection diode outputs a big enough voltage, indicating the light barrier to continue being uninterrupted
	in_sprint_mode = 1;
	laserOn();
	ESP_LOGI(TAG, "lightBarrierTask started!");
	double current_time = 0;
    esp_adc_cal_characteristics_t *adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t)); //allocated memory space for ADC conversion characteristics to be stored
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1092, adc_chars); //1092 mV: Vref (reference voltage of the development board)

	if (esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC_CHANNEL), adc_chars) < VOLTAGE_LIMIT) {
		strcpy(time_str, "nc");
		ESP_LOGE(TAG, "Optical barrier not in line for measurement!");
	} else {
		strcpy(time_str, "pending");
		redOn();
		while (in_sprint_mode == 1) {
			if (esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC_CHANNEL), adc_chars) < VOLTAGE_LIMIT) { //if diode voltage output < 400mV (threshold), current time (seconds) is stored in time_str variable & loop is exited
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

TaskHandle_t startLightBarrier() {    //function initializing the optical time measurement function on CPU core 1 (reserved only for that to avoid delays caused by other functions)
	BaseType_t taskCreateReturn = NULL;
	TaskHandle_t taskVoltageHandle = NULL;

	if (in_sprint_mode == 1) {
		taskCreateReturn = xTaskCreatePinnedToCore(
			lightBarrierTask,			//task function
			"lightBarrierTask",			//task name
			5000,						//stack size
			laser_intr_time,			//task parameters
			configMAX_PRIORITIES - 1,  //task priority
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

void alignIndicatorTask() {    //task toggling the RED status LED based on whether the optical diode detects the laser to be correctly aligned (pointed at the diode)
	in_align_mode = 1;
	ESP_LOGI(TAG, "alignIndicatorTask started!");
    esp_adc_cal_characteristics_t *adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1092, adc_chars); //1092 mV: Vref (reference voltage of the development board)
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

TaskHandle_t startAlignIndicator() {    //function initializing the alignment indicator function on CPU core 1 to avoid delays
	BaseType_t taskCreateReturn = NULL;
	TaskHandle_t taskAlignHandle = NULL;
	taskCreateReturn = xTaskCreatePinnedToCore(
		alignIndicatorTask,         //task function
		"alignIndicatorTask",       //task name
		5000,                       //stack size
		NULL,                       //task parameters
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

void syncTimerTask() {    //task rapidly taking AD measurements of optical diode; resets measurement timer if laser is detected; assigned to CPU core 1 to avoid delays
	in_sync_mode = 1;
	ESP_LOGI(TAG, "syncTimerTask started!");
	double start_time;
	timer_get_counter_time_sec(TIMER_GROUP_1, TIMER_1, &start_time);
	double current_time = start_time;
    esp_adc_cal_characteristics_t *adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1092, adc_chars); //1092 mV: Vref (reference voltage of the development board)
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

TaskHandle_t initSyncTimerTask() {    //function initializing the timer synchronization function on CPU core 1 to avoid delays
	BaseType_t taskCreateReturn;
	TaskHandle_t taskSyncTimerHandle = NULL;

	taskCreateReturn = xTaskCreatePinnedToCore(
		syncTimerTask,				//task function
		"syncTimerTask",			//task name
		5000,						//stack size
		NULL,						//task parameters
		configMAX_PRIORITIES - 1,   //task priority
		&taskSyncTimerHandle,       //task handle
		1							//task CPU core
	);
    if(taskCreateReturn == pdPASS) {
        ESP_LOGI(TAG, "syncTimerTask created successfully!");
    } else {
        ESP_LOGE(TAG, "syncTimerTask NOT created successfully!");
    }
    return taskSyncTimerHandle;
}

void CountdownBuzzerTask() {    //function playing a count down "melody" using the external buzzer
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


TaskHandle_t startCountdownBuzzer() {    //function initializing the audible count down function on CPU core 1
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

void IRAM_ATTR timer_interrupt_handler (void *HDF) {    //timer interrupt being called when its time to start the countdown
	timer_disable_intr(TIMER_GROUP_1, TIMER_1);
	timer_group_clr_intr_status_in_isr(TIMER_GROUP_1, TIMER_1);
	timer_set_alarm(TIMER_GROUP_1, TIMER_1, TIMER_ALARM_DIS);
	startCountdownBuzzer();
}

esp_err_t uri_post(httpd_req_t* req) {    //function to handle incoming HTTP POST requests
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
//#######################################################################################################################################################################

//stuctures containing information about which function to call depending on the HTTP server URL being called
//#######################################################################################################################################################################
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
//#######################################################################################################################################################################

httpd_handle_t httpserver_initialization() {    //HTTP server initialization function
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	httpd_handle_t server = NULL;
	if (httpd_start(&server, &config) == ESP_OK) {
		httpd_register_uri_handler(server, &uri_get_struct);
		httpd_register_uri_handler(server, &uri_post_struct);
	}
	return server;
}


void main_c0(void)
{
	gpio_initialization();
	adc_initialization();

	timer_initialization();
	timer_isr_register(TIMER_GROUP_1, TIMER_1, &timer_interrupt_handler, NULL, ESP_INTR_FLAG_IRAM, NULL);	//Interrupt-Funktion wird registriert

	netif_ap_initialization();
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
		18,                         //task priority
		&taskBuzzerHandle,			//task handle
		0							//task CPU core
	);
}
