#include "wifi_task.h"

static EventGroupHandle_t s_wifi_event_group;
static const int CONNECTED_BIT = BIT0;

static void wifi_init();
static void wifi_event_handler(void *, esp_event_base_t, int32_t, void *);
static void waitingTask(void *);

static void (*on_connected_handler)(char *ip_address);

static char ip_address[INET_ADDRSTRLEN];

void createWifiTask(void (*on_connected)(char *ip_address))
{
  on_connected_handler = on_connected;
  wifi_init();
}

static void wifi_init()
{
  ESP_ERROR_CHECK(nvs_flash_init());

  ESP_ERROR_CHECK(esp_netif_init());
  s_wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  assert(sta_netif);

  wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();

  ESP_ERROR_CHECK(esp_wifi_init(&wifi_config));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

  wifi_config_t sta_config = {
      .sta = {
          .ssid = "...",
          .password = "..."},
  };
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config));

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_connect());
}

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
  {
    TaskHandle_t xHandle = NULL;
    xTaskCreate(waitingTask, "waitingTask", 4096, NULL, 3, &xHandle);
    if (xHandle == NULL)
    {
      ESP_LOGE("wait", "Could not create task");
      abort();
    }
  }
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
  {
    ESP_LOGI("wait", "WiFi disconnected");
    esp_wifi_connect();
    xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
  }
  else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
  {
    const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
    snprintf(ip_address, INET_ADDRSTRLEN, IPSTR, IP2STR(&event->ip_info.ip));
    ESP_LOGI("wait", "Got IP: %s", ip_address);
    xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
  }
}

void waitingTask(void *params)
{
  EventBits_t uxBits;

  for (;;)
  {
    ESP_LOGI("wait", "WiFi Connecting");
    uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT, true, false, portMAX_DELAY);
    if (uxBits & CONNECTED_BIT)
    {
      ESP_LOGI("wait", "WiFi Connected to ap");
      on_connected_handler(ip_address);
      vTaskDelete(NULL);
    }
  }
}
