#include "perform_http_request.h"

esp_err_t _http_event_handler(esp_http_client_event_t *evt)	{	//handles HTTP client events
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(HTTP_REQ_TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(HTTP_REQ_TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(HTTP_REQ_TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(HTTP_REQ_TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(HTTP_REQ_TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else { //usually the case
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(HTTP_REQ_TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(HTTP_REQ_TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(HTTP_REQ_TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

esp_err_t perform_http_request(	int method,				//GET or POST
								char * signal,			//application specific: parameter determines how "signal" is sent to the other ESP device (at the start of the track)
								char * post_field,		//buffer containing information to be sent using the POST request body
								char headers[][2][50],	//array with every element[0] being the headers name, and element[1] being the value assigned to the header
								int header_count,		//number of headers to be sent
								char * response_buf,	//buffer containing the HTTP request response
								int response_buf_len)	//length of the HTTP response buffer
{	//function to make using ESP-IDF HTTP client requests easier
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
   	ESP_LOGI(HTTP_REQ_TAG, "HTTP request URL BUF: %s", url_buf);
	esp_http_client_set_url(client, url_buf);
	esp_http_client_get_url(client, url_buf, 100);
	ESP_LOGI(HTTP_REQ_TAG, "HTTP request URL: %s", url_buf);
    if (post_field != NULL) {
    	if (strcmp(post_field, "") != 0) {
    		esp_http_client_set_post_field(client, post_field, strlen(post_field));
    	}
	}


	esp_http_client_set_method(client, method);

	int i = 0;
	while (i < header_count) {
		esp_http_client_set_header(client, headers[i][0], headers[i][1]);
		ESP_LOGI(HTTP_REQ_TAG, "HTTP request header set: %s: %s", headers[i][0], headers[i][1]);
		i++;
	}

	esp_err_t err = esp_http_client_perform(client);	//send collected information as a HTTP client request
	if (err == ESP_OK) {
		int content_len = esp_http_client_get_content_length(client);
		ESP_LOGI(HTTP_REQ_TAG, "HTTP POST Status = %d, content_length = %d",
				esp_http_client_get_status_code(client),
				content_len);
		if (content_len < response_buf_len) {
			response_buf[content_len] = '\0'; //NULL-terminator has to be set by hand after response is transferred into buffer
		}
	} else {
		ESP_LOGE(HTTP_REQ_TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
	}
	err = esp_http_client_cleanup(client);

	return err;
}
