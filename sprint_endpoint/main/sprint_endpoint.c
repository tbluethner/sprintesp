#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
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
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"
#include "driver/timer.h"
#include "driver/ledc.h"
#include <dirent.h>
#include "driver/adc.h"
#include "esp_adc_cal.h"

#include "esp_tls_crypto.h"
#include "esp_http_server.h"
#include "esp_http_client.h"

#include <math.h> //for pow() function

#include <sys/time.h>

#include "pinout_utils.h" //custom library with GPIO toggle & PWM definitions & functions

//IMPORTANT: CDU 1 Idle task watchdog DISABLED in menuconfig -> Component config -> Common ESP-related -> Watch CPU1 Idle Task!

#define TAG "SPRINTEND"

#define SD_MODE_MMC 0
#define SD_MODE_SPI 1

#define TIMER_DIVIDER		40000  //  Hardware timer clock divider
#define TIMER_SCALE			(TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define TIMER_COUNTSTART	0
#define MASTER_URL			"http://192.168.5.1/"

#define ADC_CHANNEL			ADC1_CHANNEL_6

#define VOLTAGE_LIMIT 		400		// lower limit for laser receiver diode: 300 mV

uint8_t in_sprint_mode = 0;
uint8_t in_align_mode = 0;

char laser_intr_time[30] = ""; //string with timecount (sec)

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

esp_err_t perform_http_request(int method, char * signal, char * post_field, char headers[][2][50], int header_count, char * response_buf, int response_buf_len) {
    esp_http_client_config_t client_cfg = {
        .url = MASTER_URL,
		.user_data = response_buf,        // Pass address of local buffer to get response
		.event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&client_cfg);
    char url_buf[100];
    esp_http_client_get_url(client, url_buf, 100);

    if (method == HTTP_METHOD_GET) {
    	strcat(url_buf, "uri_get");
    } else if (method == HTTP_METHOD_POST) {
    	strcat(url_buf, "uri_post");
    }
   	if (signal != NULL) {
    	if (strcmp(signal, "") != 0) {
    		strcat(url_buf, "?signal=");
    		strcat(url_buf, signal);
    	}
	}
   	ESP_LOGI(TAG, "HTTP request URL BUF: %s", url_buf);
	esp_http_client_set_url(client, url_buf);
	esp_http_client_get_url(client, url_buf, 100);
	ESP_LOGI(TAG, "HTTP request URL: %s", url_buf);
    if (post_field != NULL) {
    	if (strcmp(post_field, "") != 0) {
    		esp_http_client_set_post_field(client, post_field, strlen(post_field));
    	}
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
		int content_len = esp_http_client_get_content_length(client);
		ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
				esp_http_client_get_status_code(client),
				content_len);
		if (content_len < response_buf_len) {
			response_buf[content_len] = '\0'; //NULL-terminator has to be set by hand after response is transferred into buffer
		}
	} else {
		ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
	}
	err = esp_http_client_cleanup(client);

	return err;
}

int getFileStringLength(char * folder) {
	int fileStringLength = 0;
	int folder_name_len = strlen(folder);
	char path[strlen("/sdcard") + folder_name_len];
	strcpy(path, "/sdcard");
	strcat(path, folder);
	DIR * dir = opendir(path);
	struct dirent *diread;
	if (dir != NULL) {
		fileStringLength += 1;	//+1 for semicolon at the beginning of the string
		while ((diread = readdir(dir)) != NULL) {
			if (strcmp(diread->d_name, "favicon.ico") != 0) {
				fileStringLength += strlen(diread->d_name) + 1;	//+1 for semicolons added between filenames in listFiles()
			};
		}
	}
	fileStringLength += 1;	//NULL terminator
	closedir(dir);
	return fileStringLength;
}

esp_err_t listFiles(char * given_output, char * folder) {
	given_output[0] = '\0';
	int folder_name_len = strlen(folder);
	char path[strlen("/sdcard") + folder_name_len];
	strcpy(path, "/sdcard");
	strcat(path, folder);
	DIR * dir = opendir(path);
	struct dirent *diread;
	if (dir != NULL) {
		strcat(given_output, ";");
		while ((diread = readdir(dir)) != NULL) {
			if (strcmp(diread->d_name, "favicon.ico") != 0) {
				strcat(given_output, diread->d_name);
				strcat(given_output, ";");
			}
		}
	}
	closedir(dir);
	return ESP_OK;
}

esp_err_t sendFile(httpd_req_t * req, int filename_len, char * filename) {
    char path_to_file[sizeof("/sdcard/") + filename_len];
    strcpy(path_to_file, "/sdcard/");
    strcat(path_to_file, filename);
	const size_t bytes_to_read = 100;
	size_t bytes_read = 100;
		FILE * file = fopen(path_to_file, "r");
	    if (file != NULL) {
	    	ESP_LOGI(TAG, "File on SD Card opened SUCESSFULLY");
			char buffer[100];
			while (bytes_to_read == bytes_read) {
				bytes_read = fread(buffer, 1, bytes_to_read, file);
				httpd_resp_send_chunk(req, buffer, bytes_read);
			}
			httpd_resp_send_chunk(req, NULL, 0);
	    } else {
	    	ESP_LOGE(TAG, "File on SD Card NOT opened SUCESSFULLY");
	    	httpd_resp_sendstr(req, "File on SD Card NOT opened SUCESSFULLY");
	    }
    fclose(file);
	return ESP_OK;
}

uint8_t connection_status_check() {
	uint8_t status = 0;
	wifi_ap_record_t connected_ap_info;
	if (esp_wifi_sta_get_ap_info(&connected_ap_info) == ESP_OK) {
		status = 1;
		blueOn();
	} else {
		blueBlinking();
	}
	return status;
}

esp_err_t uri_get(httpd_req_t* req)
{
    size_t buf_len = httpd_req_get_url_query_len(req) + 1; // +1 for NULL termination
    char signal_param[50];

	if (buf_len > 1) {
		char * buf = malloc(buf_len);
		if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
			if (httpd_query_key_value(buf, "signal", signal_param, sizeof(signal_param)) == ESP_OK) {
				if (strcmp(signal_param, "getWifiStatus") == 0) {           //0: exact match
					if (connection_status_check() == 1) {
						httpd_resp_sendstr(req, "connected");
					} else {
						httpd_resp_sendstr(req, "disconnected");
					}
				}
				if (strcmp(signal_param, "getEndTimecount") == 0) {           //0: exact match
					double current_time = 0;
					char time_str[30];
					timer_get_counter_time_sec(TIMER_GROUP_1, TIMER_1, &current_time);
					sprintf(time_str, "%.3f", current_time);
					httpd_resp_sendstr(req, time_str);
				}
				if (strcmp(signal_param, "getStartTimecount") == 0) {           //0: exact match
					if (connection_status_check() == 1) {
						char data_buf[30];
						perform_http_request(HTTP_METHOD_GET, "getStartTimecount", NULL, NULL, 0, data_buf, sizeof(data_buf)); //this request is a passthrough of the result of a http request to the START device
						httpd_resp_sendstr(req, data_buf);
					} else {
						httpd_resp_sendstr(req, "NC");
					}
				}
				if (strcmp(signal_param, "getStartLaserIntr") == 0) {           //0: exact match
					if (connection_status_check() == 1) {
						char data_buf[30];
						perform_http_request(HTTP_METHOD_GET, "getStartLastLaserIntr", NULL, NULL, 0, data_buf, sizeof(data_buf)); //this request is a passthrough of the result of a http request to the START device
						httpd_resp_sendstr(req, data_buf);
					} else {
						httpd_resp_sendstr(req, "NC");
					}
				}
				if (strcmp(signal_param, "getEndLaserIntr") == 0) {           //0: exact match
					ESP_LOGI(TAG, "Last End Laser Intr Timestring: %s\n", laser_intr_time);
					httpd_resp_sendstr(req, laser_intr_time);
				}
				if (strcmp(signal_param, "getSprintStatus") == 0) {           //0: exact match
					if (in_sprint_mode == 1) {
						httpd_resp_sendstr(req, "active");
					} else {
						httpd_resp_sendstr(req, "inactive");
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

esp_err_t downloadFile(httpd_req_t* req)
{
    size_t buf_len = httpd_req_get_url_query_len(req) + 1; //	NULL termination
    char filename_param[50];
	if (buf_len > 1) {
		char * buf = malloc(buf_len);
		if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
			if (httpd_query_key_value(buf, "filename", filename_param, sizeof(filename_param)) == ESP_OK) {
				sendFile(req, strlen(filename_param), filename_param);
			}
		}
	}

	return ESP_OK;
}

esp_err_t getindexhtml(httpd_req_t* req)
{
	char filename[] = "index.html";
	sendFile(req, strlen(filename), filename);
	return ESP_OK;
}

esp_err_t getfavicon(httpd_req_t* req)
{
	httpd_resp_set_type(req, "image/x-icon");
	char path_to_file[] = "/sdcard/favicon.ico";
	const size_t bytes_to_read = 100; //count
	size_t bytes_read = 100;
    FILE * file = fopen(path_to_file, "r");
    char buffer[100];
    while (bytes_to_read == bytes_read) {
    	bytes_read = fread(buffer, 1, bytes_to_read, file);
    	httpd_resp_send_chunk(req, buffer, bytes_read);
    }
    httpd_resp_send_chunk(req, NULL, 0);
    fclose(file);
	return ESP_OK;
}

esp_err_t html_edit(httpd_req_t* req)
{
	char filename[] = "html_edit.html";
	sendFile(req, strlen(filename), filename);
	return ESP_OK;
}

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

esp_err_t uri_post(httpd_req_t* req)
{
    size_t buf_len = httpd_req_get_url_query_len(req) + 1; // +1 for NULL termination
    char signal_param[50];

	if (buf_len > 1) {
		char * buf = malloc(buf_len);
		if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
			if (httpd_query_key_value(buf, "signal", signal_param, sizeof(signal_param)) == ESP_OK) {
				if (strcmp(signal_param, "fileCreate") == 0) {           //0: exact match
					size_t filename_len = httpd_req_get_hdr_value_len(req, "Filename") + 1; //header contains filename
				    if (filename_len > 1) {
				        char * filename_buf = malloc(filename_len);
				        /* Copy null terminated value string into buffer */
				        if (httpd_req_get_hdr_value_str(req, "Filename", filename_buf, filename_len) == ESP_OK) {
							int fileStringLength = getFileStringLength("");
							char fileString[fileStringLength];
							char illegal_characters[] = "\\üöä";
							listFiles(&fileString, "");
				        	if (strpbrk(filename_buf, illegal_characters) != NULL) {  //try to find any of the characters in illegal_characters[] in the given filename
				        		httpd_resp_sendstr(req, "Filename contains illegal characters!");
				        		free(filename_buf);
				        	} else if (strstr(fileString, filename_buf) != NULL) {
				        		httpd_resp_sendstr(req, "Filename already existing!");
				        		free(filename_buf);
				        	} else {
								//if checks showed no bad results, file creation can proceed as planned
								//file creation = fopen & fclose file not yet existing
								char path_to_file[sizeof("/sdcard/") + filename_len];
								strcpy(path_to_file, "/sdcard/");
								strcat(path_to_file, filename_buf);
								FILE * textfile = fopen(path_to_file, "w");
								fclose(textfile);
								httpd_resp_sendstr(req, "File probably created successfully!");
								free(filename_buf);
				        	}
				        }
				    }
				}
				if (strcmp(signal_param, "fileDelete") == 0) {           //0: exact match
					size_t filename_len = httpd_req_get_hdr_value_len(req, "Filename") + 1; //header contains filename
				    if (filename_len > 1) {
				        char * filename_buf = malloc(filename_len);
				        /* Copy null terminated value string into buffer */
				        if (httpd_req_get_hdr_value_str(req, "Filename", filename_buf, filename_len) == ESP_OK) {
							int fileStringLength = getFileStringLength("");
							char fileString[fileStringLength];
							char illegal_characters[] = "\\üöä";
							listFiles(&fileString, "");
							if ((strcmp(filename_buf, "favicon.ico") == 0) || (strcmp(filename_buf, "html_edit.html") == 0) || (strcmp(filename_buf, "index.html") == 0) || (strcmp(filename_buf, "wlan.cfg") == 0) || (strcmp(filename_buf, "environment.log") == 0)) {
				        		httpd_resp_sendstr(req, "Deleting the selected file is forbidden!");
				        		free(filename_buf);
							} else if (strpbrk(filename_buf, illegal_characters) != NULL) {  //try to find any of the characters in illegal_characters[] in the given filename
				        		httpd_resp_sendstr(req, "Filename contains illegal characters!");
				        		free(filename_buf);
				        	} else if (strstr(fileString, filename_buf) == NULL) {
				        		httpd_resp_sendstr(req, "File not found! Try to reload the website!");
				        		free(filename_buf);
				        	} else {
								//if checks showed no bad results, file creation can proceed as planned
								//file creation = fopen & fclose file not yet existing
								char path_to_file[sizeof("/sdcard/") + filename_len];
								strcpy(path_to_file, "/sdcard/");
								strcat(path_to_file, filename_buf);
								if (remove(path_to_file) != 0) {
									httpd_resp_sendstr(req, "Deleting NOT successful!");
								} else {
									httpd_resp_sendstr(req, "Deleting successful!");
								}
								free(filename_buf);
				        	}
				        }
				    }
				}
				if (strcmp(signal_param, "filenamesRead") == 0) {           //0: exact match
					size_t foldername_len = httpd_req_get_hdr_value_len(req, "Foldername") + 1; //header contains filename
			        char * folder_buf = malloc(foldername_len);
			        if (httpd_req_get_hdr_value_str(req, "Foldername", folder_buf, foldername_len) == ESP_OK) {
						int fileStringLength = getFileStringLength(folder_buf);
						char fileString[fileStringLength];
						listFiles(&fileString, folder_buf);
						httpd_resp_send(req, fileString, fileStringLength);
			        }
					free(folder_buf);
				}
				if (strcmp(signal_param, "fileWrite") == 0) {           //0: exact match
					size_t filename_len = httpd_req_get_hdr_value_len(req, "Filename") + 1; //header contains filename
				    if (filename_len > 1) {
				        char * filename_buf = malloc(filename_len);
				        /* Copy null terminated value string into buffer */
				        if (httpd_req_get_hdr_value_str(req, "Filename", filename_buf, filename_len) == ESP_OK) {
					        char path_to_file[sizeof("/sdcard/") + filename_len];
					        strcpy(path_to_file, "/sdcard/");
					        strcat(path_to_file, filename_buf);
					        FILE * textfile = fopen(path_to_file, "w");
					        fclose(textfile);
					        textfile = fopen(path_to_file, "a");
					        char content[100];
					        int ret;
					        int content_remaining = req->content_len;

					        while (content_remaining > 0) {
					            // Read the data for the request
					            if ((ret = httpd_req_recv(req, content, sizeof(content))) <= 0) {
					                if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
					                    // Retry receiving if timeout occurred
					                	ESP_LOGI(TAG, "TIMEOUT");
							            ret = 0;
					                    continue;
					                }
					                return ESP_FAIL;
					            }
					            // Send back the same data
					            content_remaining -= ret;
					            if (ret < sizeof(content)) {
					            	content[ret] = '\0';
					            }
					            fputs(content, textfile);
					        }

					        fclose(textfile);
					    	ESP_LOGI(TAG, "File on SD Card written (success uncertain)");

					        httpd_resp_sendstr(req, "Writing probably successful! :)");
				        }
				        free(filename_buf);
				    }
				}
				if (strcmp(signal_param, "fileRead") == 0) {           //0: exact match
					size_t filename_len = httpd_req_get_hdr_value_len(req, "Filename") + 1; //header contains filename
				    if (filename_len > 1) {
				        char filename_buf[filename_len];
				        if (httpd_req_get_hdr_value_str(req, "Filename", filename_buf, filename_len) == ESP_OK) {
				        	sendFile(req, filename_len, filename_buf);

				        }
				    }
					httpd_resp_sendstr(req, "Filename not found in header data!");
				}
				if (strcmp(signal_param, "toggleWifi") == 0) {           //0: exact match
					if (connection_status_check() == 1) {
						httpd_resp_sendstr(req, "Connection ALREADY established!");
					} else {
						if (esp_wifi_connect() != ESP_OK) {
							httpd_resp_sendstr(req, "WiFi connection configuration error!");
						} else {
							httpd_resp_sendstr(req, "Connecting...");
						}
					}
				}
				if (strcmp(signal_param, "syncTimers") == 0) {           //0: exact match
					char data_buf[30];
					perform_http_request(HTTP_METHOD_POST, "startTimeSync", NULL, NULL, 0, data_buf, sizeof(data_buf));
					if (strcmp(data_buf, "started") == 0) {
						httpd_resp_sendstr(req, "Time sync started!");
					} else if (strcmp(data_buf, "underway") == 0) {
						httpd_resp_sendstr(req, "Time sync already started!");
					} else {
						httpd_resp_sendstr(req, "Time sync ERROR!");
					}
					vTaskDelay(2000 / portTICK_PERIOD_MS);
					laserOn();
					timer_set_counter_value(TIMER_GROUP_1, TIMER_1, (TIMER_COUNTSTART * TIMER_SCALE));
				    buzzerOn(0);
				    greenOn();
				    vTaskDelay(500 / portTICK_PERIOD_MS);
				    buzzerOff();
				    greenOff();
				    vTaskDelay(500 / portTICK_PERIOD_MS);

				    buzzerOn(0);
				    greenOn();
				    vTaskDelay(100 / portTICK_PERIOD_MS);
				    buzzerOff();
				    greenOff();
				    vTaskDelay(100 / portTICK_PERIOD_MS);
				    buzzerOn(0);
				    greenOn();
				    vTaskDelay(100 / portTICK_PERIOD_MS);
				    buzzerOff();
				    greenOff();
				    vTaskDelay(100 / portTICK_PERIOD_MS);
				    buzzerOn(0);
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
				    greenBlinking();

					laserOff();
				}
				if (strcmp(signal_param, "startSprint") == 0) {           //0: exact match
					size_t seconds_len = httpd_req_get_hdr_value_len(req, "seconds") + 1; //header contains the second mark that the synced devices have to pass so they start their buzzer
				    if (seconds_len > 1) {
				        char * seconds_buf = malloc(seconds_len);
				        if (httpd_req_get_hdr_value_str(req, "seconds", seconds_buf, seconds_len) == ESP_OK) {
				        	int seconds_int = charToInt(seconds_buf);
				        	char data_buf[30];
				        	char headers[10][2][50];
				        	strcpy(headers[0][0], "seconds");
				        	strcpy(headers[0][1], seconds_buf);

				        	if (in_sprint_mode == 0) {
								perform_http_request(HTTP_METHOD_POST, "startSprint", NULL, headers, 1, data_buf, sizeof(data_buf));
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
					perform_http_request(HTTP_METHOD_POST, "stopSprint", NULL, NULL, 0, NULL, 0);
					httpd_resp_sendstr(req, "stopped");
				}
				if (strcmp(signal_param, "startAlign") == 0) {           //0: exact match
				    laserOn();
		        	char data_buf[30];
					perform_http_request(HTTP_METHOD_POST, "startAlign", NULL, NULL, 0, data_buf, sizeof(data_buf));
					if (in_align_mode == 0) {
						startAlignIndicator();
						httpd_resp_sendstr(req, "started");
					} else {
						httpd_resp_sendstr(req, "pending");
					}
				}
				if (strcmp(signal_param, "stopAlign") == 0) {           //0: exact match
					laserOff();
		        	char data_buf[30];
					perform_http_request(HTTP_METHOD_POST, "stopAlign", NULL, NULL, 0, data_buf, sizeof(data_buf));
					in_align_mode = 0;
					httpd_resp_sendstr(req, "stopped");
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

httpd_uri_t uri_get_html_edit = {
    .uri      = "/html_edit",
    .method   = HTTP_GET,
    .handler  = html_edit,
    .user_ctx = NULL
};

httpd_uri_t uri_get_index = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = getindexhtml,
    .user_ctx = NULL
};

httpd_uri_t uri_get_favicon = {
    .uri      = "/favicon.ico",
    .method   = HTTP_GET,
    .handler  = getfavicon,
    .user_ctx = NULL
};

httpd_uri_t uri_get_download = {
    .uri      = "/download",
    .method   = HTTP_GET,
    .handler  = downloadFile,
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
		httpd_register_uri_handler(server, &uri_get_html_edit);
		httpd_register_uri_handler(server, &uri_get_index);
		httpd_register_uri_handler(server, &uri_get_favicon);
		httpd_register_uri_handler(server, &uri_get_download);
		httpd_register_uri_handler(server, &uri_post_struct);
	}
	return server;
}


esp_err_t sd_initialization(uint8_t sd_mode) {
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
			ESP_LOGE(TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
			return;
		}

		if ((ret = esp_wifi_set_mode(WIFI_MODE_APSTA)) != ESP_OK) {
			ESP_LOGE(TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
			return;
		}
		//SET PROTOCOLS ###########
		if ((ret = esp_wifi_set_protocol(ESP_IF_WIFI_AP, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N)) != ESP_OK) {
			ESP_LOGE(TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
			return;
		}
		if ((ret = esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N)) != ESP_OK) {
			ESP_LOGE(TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
			return;
		}
		//#########################

		if ((ret = esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_wifi_conf)) != ESP_OK) {
			ESP_LOGE(TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
			return;
		}

		if ((ret = esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_wifi_conf)) != ESP_OK) {
			ESP_LOGE(TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
			return;
		}

		if ((ret = esp_wifi_start()) != ESP_OK) {
			ESP_LOGE(TAG, "%s wifi init failed: %s\n", __func__, esp_err_to_name(ret));
			return;
		}


		if ((ret = esp_wifi_connect()) != ESP_OK) {
			ESP_LOGE(TAG, "%s wifi connect failed: %s\n", __func__, esp_err_to_name(ret));
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
	timer_initialization();
	adc_initialization();
	esp_err_t status_sd_initialization = sd_initialization(SD_MODE_SPI);

	if (status_sd_initialization == ESP_OK) {
		httpd_handle_t http_server_handle = NULL;
		netif_ap_initialization();
		netif_sta_initialization();
		wifi_initialization();
		http_server_handle = httpserver_initialization();
		connection_status_check();
	}

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
