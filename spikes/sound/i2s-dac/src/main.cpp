#ifndef ESP32
#define ESP32 1
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>

#include "FreeRTOS.h"
#include "WM8960.h"
#include "driver/i2s.h"
#include "freertos/ringbuf.h"
#include "lwip/sockets.h"

const char *ssid = "...";
const char *pwd = "...";

static const i2s_port_t i2s_num = I2S_NUM_0;  // i2s port number

static const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT, /* the DAC module will only
                                                     take the 8bits from MSB */
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = 0,  // default interrupt priority
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false};

static const i2s_pin_config_t pin_config = {.bck_io_num = I2S_CLK,
                                            .ws_io_num = I2S_WS,
                                            .data_out_num = I2S_TXSDA,
                                            .data_in_num = I2S_PIN_NO_CHANGE};

bool connected = false;
RingbufHandle_t buffer;

void dataIntakeTask(void *);

void tcpServerTask(void *pvParameters) {
  // byte rxBuffer[512];
  sockaddr_in localAddress;

  // Configure IPv4 address
  localAddress.sin_addr.s_addr = htonl(INADDR_ANY);
  localAddress.sin_family = AF_INET;
  localAddress.sin_port = htons(2137);

  int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (sock < 1) {
    Serial.printf("Unable to create socket, error code: %d\n", errno);
    vTaskDelete(NULL);
    return;
  }
  Serial.println("Socket created successfully");

  if (bind(sock, (sockaddr *)&localAddress, sizeof(localAddress)) < 0) {
    Serial.printf("Unable to bind socket, error code: %d\n", errno);
    vTaskDelete(NULL);
    return;
  }
  Serial.println("Socket bound successfully");

  if (listen(sock, 2) != 0) {
    Serial.printf("Unable to listen on socket, error code: %d\n", errno);
    vTaskDelete(NULL);
    return;
  }
  Serial.println("Socket listening");

  while (true) {
    sockaddr_in remoteAddress;
    socklen_t socklen = sizeof(remoteAddress);

    int *remoteSock = new int;
    *remoteSock = accept(sock, (sockaddr *)&remoteAddress, &socklen);
    if (*remoteSock < 0) {
      Serial.printf("Unable to accept connection, error code: %d\n", errno);
      continue;
    }
    Serial.println("Got connection");

    xTaskCreate(dataIntakeTask, "DataIntake", 1 << 15, (void *)remoteSock, 1,
                NULL);

    // int len = recvfrom(sock, rxBuffer, sizeof(rxBuffer), 0,
    //                    (sockaddr *)&remoteAddress, &socklen);

    // if (len < 0) {
    //   Serial.printf("recvfrom failed, error code: %d\n", errno);
    //   continue;
    // }
    // Serial.printf("Received %d bytes\n", len);

    // Send received data to the ring buffer
    // while (xRingbufferSend(buffer, rxBuffer, len, pdMS_TO_TICKS(100)) !=
    //        pdTRUE) {
    //   // Serial.println("Failed to write bytes to buffer");
    //   vTaskDelay(pdMS_TO_TICKS(100));
    // }
  }

  vTaskDelete(NULL);
}

void dataIntakeTask(void *pvParameters) {
  int *remoteSock = (int *)pvParameters;
  byte rxData[8192];

  while (true) {
    int len = recv(*remoteSock, rxData, sizeof(rxData), 0);

    if (len < 0) {
      Serial.println("Error while receiving network data");
      continue;
    }

    if (len == 0) {
      Serial.println("Connection closed");
      close(*remoteSock);
      delete remoteSock;
      break;
    }

    // Send received data to the ring buffer
    while (xRingbufferSend(buffer, rxData, len, pdMS_TO_TICKS(100)) != pdTRUE) {
      // Serial.println("Failed to write bytes to buffer");
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  vTaskDelete(NULL);
}

void i2sTask(void *pvParameters) {
  while (true) {
    size_t readSize;
    byte *data = (byte *)xRingbufferReceiveUpTo(buffer, &readSize,
                                                pdMS_TO_TICKS(100), 8192);

    if (data != NULL) {
      // Serial.printf("Read %d bytes:\n", readSize);
      // for (int i = 0; i < readSize; i++) {
      //   printf("%c", data[i]);
      // }
      // printf("\n");

      size_t writtenSize = 0;
      while (writtenSize < readSize) {
        esp_err_t err =
            i2s_write(i2s_num, data + writtenSize, readSize - writtenSize,
                      &writtenSize, pdMS_TO_TICKS(100));
        // Serial.printf("Wrote %d bytes so far\n", writtenSize);
        if (err) {
          Serial.printf("Error while pushing data to DAC: %d\n", err);
        }
      }

      vRingbufferReturnItem(buffer, (void *)data);
    } else {
      // Serial.println("Failed to read data");
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}

static void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_STA_GOT_IP:
      Serial.print("WiFi connected! IP address: ");
      Serial.println(WiFi.localIP());

      xTaskCreate(tcpServerTask, "Server", 8192, NULL, 1, NULL);
      connected = true;
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      Serial.println("WiFi lost connection");
      connected = false;
      break;
    default:
      break;
  }
}

void connectToWiFi(const char *ssid, const char *pwd) {
  Serial.println("Connecting to WiFi network: " + String(ssid));
  WiFi.disconnect(true);
  WiFi.onEvent(WiFiEvent);
  WiFi.begin(ssid, pwd);
  Serial.println("Waiting for WIFI connection...");
}

void setup() {
  Serial.begin(9600);
  Wire.begin(I2C_SDA, I2C_SCL);

  byte wm_init_result = WM8960.begin();
  if (wm_init_result == 0) {
    Serial.printf("DAC initialized successfully\n");
  } else {
    Serial.printf("DAC initialization failed: %d\n", wm_init_result);
  }

  buffer = xRingbufferCreate(1 << 16, RINGBUF_TYPE_BYTEBUF);
  xRingbufferPrintInfo(buffer);

  connectToWiFi(ssid, pwd);

  i2s_driver_install(i2s_num, &i2s_config, 0, NULL);
  i2s_set_pin(i2s_num, &pin_config);
  i2s_set_sample_rates(i2s_num, 44100);

  xTaskCreate(i2sTask, "I2S", 10000, NULL, 1, NULL);
}

void loop() {
  if (!connected) return;

  while (true) {
    // Serial.println("Loop task");
    delay(1000);
  }
}