#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_client.h"

#define MASTER_URL      "http://192.168.5.1/"
#define HTTP_REQ_TAG    "PERF_HTTP_REQ"

esp_err_t _http_event_handler(esp_http_client_event_t *evt);
esp_err_t perform_http_request(int method, char * signal, char * post_field, char headers[][2][50], int header_count, char * response_buf, int response_buf_len);
