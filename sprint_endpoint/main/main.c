//IMPORTANT: CDU 1 Idle task watchdog DISABLED in MENUCONFIG -> Component config -> Common ESP-related -> Watch CPU1 Idle Task!

#include "freertos/FreeRTOS.h"		//FreeRTOS = task scheduling system (also for using multiple CPU cores)
#include "esp_log.h"			//ESP LOG messages (over UART)
#include <stdio.h>
#include <string.h>
#include "esp_adc_cal.h"		//ESP analog digital conversion (precise)
#include "esp_http_server.h"		//ESP HTTP server
#include <math.h> 			//for pow() function

#include "hardware_accessibility.h" 	//custom library to utilize GPIO, PWM, ADC, Timer, WIFI
#include "perform_http_request.h"	//custom library to utilize http client functions to communicate with the sprint measurement START device

#define TAG "SPRINTEND"			//TAG printed before all UART log messages
#define VOLTAGE_LIMIT 		400	//lower limit for laser receiver diode: 400 mV

uint8_t in_sprint_mode = 0;		//flag indicating the sprint measurement process being active
uint8_t in_align_mode = 0;		//flag indicating the laser alignment process being active

char laser_intr_time[30] = "";		//string containing the time (seconds) the light barrier has been interrupted last

int charToInt(char * array) { 		//helper function converting string to integer
	int array_len = strlen(array);
	int result = 0;
	for (int i = 0; i < array_len; i++) {
		result += (array[i] - '0') * (pow(10, (array_len - (i+1))));
	}

	return result;
}

//HTTP_GET-related functions
//#######################################################################################################################################################################
uint8_t connection_status_check() {	//helper function to check for a WIFI connection between sprint measurement START and END devices
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

esp_err_t uri_get(httpd_req_t* req) {	//helper function to process incoming HTTP GET requests
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
//#######################################################################################################################################################################

//HTTP_POST-related functions
//#######################################################################################################################################################################
int getFileStringLength(char * folder) {	//helper function returning the expected length of a string containing all filenames of a directory, separated by ";"
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

esp_err_t listFiles(char * given_output, char * folder) {	//function returning a string containing all filenames of a directory, separated by ";"
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

esp_err_t sendFile(httpd_req_t * req, int filename_len, char * filename) {	//function to send a file via HTTP
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

esp_err_t downloadFile(httpd_req_t* req) {	//function to send a file (specified in the URL) for the requesting HTTP client to download
    size_t buf_len = httpd_req_get_url_query_len(req) + 1; // +1 for NULL termination
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

esp_err_t getindexhtml(httpd_req_t* req) {	//function to send the index.html file to the requesting HTTP client
	char filename[] = "index.html";
	sendFile(req, strlen(filename), filename);
	return ESP_OK;
}

esp_err_t getfavicon(httpd_req_t* req) {		//function to send the favicon.ico file to the requesting HTTP client
	httpd_resp_set_type(req, "image/x-icon");	//specifies file type in HTTP header
	char path_to_file[] = "/sdcard/favicon.ico";
	const size_t bytes_to_read = 100;
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

esp_err_t html_edit(httpd_req_t* req) {	//function to send the html_edit.html file to the requesting HTTP client
	char filename[] = "html_edit.html";
	sendFile(req, strlen(filename), filename);
	return ESP_OK;
}

void lightBarrierTask(char * time_str) {	//function repeatedly checking whether the optical detection diode outputs a big enough voltage, indicating the light barrier to continue being uninterrupted
	in_sprint_mode = 1;
	laserOn();
	ESP_LOGI(TAG, "lightBarrierTask started!");
	double current_time = 0;
    esp_adc_cal_characteristics_t *adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));	//allocated memory space for ADC conversion characteristics to be stored
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


TaskHandle_t startLightBarrier() {	//function initializing the optical time measurement function on CPU core 1 (reserved only for that to avoid delays caused by other functions)
	BaseType_t taskCreateReturn = NULL;
	TaskHandle_t taskVoltageHandle = NULL;

	if (in_sprint_mode == 1) {
		taskCreateReturn = xTaskCreatePinnedToCore(
			lightBarrierTask,			//task function
			"lightBarrierTask",			//task name
			5000,						//stack size
			laser_intr_time,			//task parameters
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

void alignIndicatorTask() {		//task toggling the RED status LED based on whether the optical diode detects the laser to be correctly aligned (pointed at the diode)
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


TaskHandle_t startAlignIndicator() {	//function initializing the alignment indicator function on CPU core 1 to avoid delays
	BaseType_t taskCreateReturn = NULL;
	TaskHandle_t taskAlignHandle = NULL;
	taskCreateReturn = xTaskCreatePinnedToCore(
		alignIndicatorTask,			//task function
		"alignIndicatorTask",		//task name
		5000,						//stack size
		NULL,						//task parameters
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

void CountdownBuzzerTask() {	//function playing a count down "melody" using the external buzzer
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


TaskHandle_t startCountdownBuzzer() { //function initializing the audible count down function on CPU core 1
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

void IRAM_ATTR timer_interrupt_handler (void *notrelevant) {	//timer interrupt being called when its time to start the countdown
	timer_disable_intr(TIMER_GROUP_1, TIMER_1);
	timer_group_clr_intr_status_in_isr(TIMER_GROUP_1, TIMER_1);
	timer_set_alarm(TIMER_GROUP_1, TIMER_1, TIMER_ALARM_DIS);
	startCountdownBuzzer();
}

esp_err_t uri_post(httpd_req_t* req) {	//function to handle incoming HTTP POST requests
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
//#######################################################################################################################################################################

//stuctures containing information about which function to call depending on the HTTP server URL being called
//#######################################################################################################################################################################
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
//#######################################################################################################################################################################

httpd_handle_t httpserver_initialization() {	//HTTP server initialization function
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

void main_c0(void)	//function going through the initialization process
{
	gpio_initialization();

	timer_initialization();
	timer_isr_register(TIMER_GROUP_1, TIMER_1, &timer_interrupt_handler, NULL, ESP_INTR_FLAG_IRAM, NULL);	//Interrupt-Funktion wird registriert

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

void app_main(void) {	//first function being called on system startup, initializing the main_c0 function (going through the initialization process) to CPU core 0
	BaseType_t taskCreateReturn = NULL;
	TaskHandle_t taskBuzzerHandle = NULL;

	taskCreateReturn = xTaskCreatePinnedToCore(
		main_c0,					//task function
		"maintask_core0",			//task name
		3584,						//stack size
		NULL,						//task parameters
		18,							//task priority
		&taskBuzzerHandle,			//task handle
		0							//task CPU core
	);
}
