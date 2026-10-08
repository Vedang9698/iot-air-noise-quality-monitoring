#include <WiFi.h>
#include <PubSubClient.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"
#include <driver/i2s.h>
#include <math.h>

// WiFi

const char* WIFI_SSID = "Hostspot";
const char* WIFI_PASSWORD = "12345678@";

// MQTT

const char* MQTT_HOSTNAME = "Vedang9698";
const int MQTT_PORT = 1883;
const char* DEVICE_ID = "ESP32_02";
const char* MQTT_TOPIC = "esp32/02/sensors";

WiFiClient espClient;
PubSubClient client(espClient);
IPAddress MQTT_BROKER;

// MQ-2

const int MQ2_PIN = 35;
const float MQ2_RL = 5000.0;
const float MQ2_R0 = 10000.0;

// MQ-2 Functions

float readMQ2PPM() {

  int adcValue = analogRead(MQ2_PIN);

  float voltage =
      (adcValue / 4095.0) * 3.3;

  if (voltage <= 0.01) {
    return 0;
  }

  float Rs =
      MQ2_RL * (5.0 - voltage) / voltage;

  float ratio =
      Rs / MQ2_R0;

  float ppm =
      574.25 * pow(ratio, -2.222);

  if (isnan(ppm) || isinf(ppm) || ppm < 0) {
    ppm = 0;
  }

  return ppm;
}

// BME680

Adafruit_BME680 bme(&Wire);

// VOC

float calculateVOC(float gas_kohms) {

  if (gas_kohms <= 0) {
    return 0;
  }

  return 100.0 / gas_kohms;
}

// INMP441

#define I2S_WS 25
#define I2S_SD 32
#define I2S_SCK 33
#define I2S_PORT I2S_NUM_0

#define SAMPLE_RATE 44100
#define BLOCK_SIZE 1024
#define CAL_OFFSET 126.5

int32_t samples[BLOCK_SIZE];

// I2S Setup

void i2s_install() {

  const i2s_config_t i2s_cfg = {

    .mode =
        (i2s_mode_t)(
          I2S_MODE_MASTER |
          I2S_MODE_RX
        ),

    .sample_rate =
        SAMPLE_RATE,

    .bits_per_sample =
        I2S_BITS_PER_SAMPLE_32BIT,

    .channel_format =
        I2S_CHANNEL_FMT_ONLY_RIGHT,

    .communication_format =
        I2S_COMM_FORMAT_I2S,

    .intr_alloc_flags =
        0,

    .dma_buf_count =
        4,

    .dma_buf_len =
        BLOCK_SIZE,

    .use_apll =
        false
  };

  const i2s_pin_config_t pin_cfg = {

    .bck_io_num =
        I2S_SCK,

    .ws_io_num =
        I2S_WS,

    .data_out_num =
        I2S_PIN_NO_CHANGE,

    .data_in_num =
        I2S_SD
  };

  i2s_driver_install(
    I2S_PORT,
    &i2s_cfg,
    0,
    NULL
  );

  i2s_set_pin(
    I2S_PORT,
    &pin_cfg
  );

  i2s_zero_dma_buffer(
    I2S_PORT
  );
}

// Sound

float readSoundLevel() {

  size_t bytes_read;

  i2s_read(
    I2S_PORT,
    (char*)samples,
    sizeof(samples),
    &bytes_read,
    portMAX_DELAY
  );

  int num_samples =
      bytes_read / sizeof(int32_t);

  double sum_squares = 0;

  for (int i = 0; i < num_samples; i++) {

    double s =
        (double)samples[i] / 2147483648.0;

    sum_squares +=
        s * s;
  }

  if (num_samples <= 0) {
    return 0;
  }

  double rms =
      sqrt(
        sum_squares /
        num_samples
      );

  if (rms < 1e-8) {
    rms = 1e-8;
  }

  float dB2 =
      20.0 *
      log10(rms) +
      CAL_OFFSET;

  Serial.printf(
    "Microphone RMS: %.8f\n",
    rms
  );

  return dB2;
}

// WiFi Connection

void connectWiFi() {

  Serial.print(
    "Connecting to WiFi"
  );

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  while (
    WiFi.status() != WL_CONNECTED
  ) {

    delay(500);

    Serial.print(".");
  }

  Serial.println();

  Serial.println(
    "WiFi connected!"
  );

  Serial.print(
    "ESP32 IP address: "
  );

  Serial.println(
    WiFi.localIP()
  );
}

// mDNS

bool findRaspberryPi() {

  Serial.println();

  Serial.println(
    "Starting mDNS..."
  );

  if (!MDNS.begin("esp32-02")) {

    Serial.println(
      "ERROR: mDNS initialization failed!"
    );

    return false;
  }

  Serial.println(
    "mDNS initialized."
  );

  Serial.println(
    "Searching for Vedang9698.local..."
  );

  IPAddress piIP =
      MDNS.queryHost(
        MQTT_HOSTNAME,
        5000
      );

  if (piIP == INADDR_NONE) {

    Serial.println(
      "ERROR: Raspberry Pi not found!"
    );

    return false;
  }

  MQTT_BROKER =
      piIP;

  Serial.print(
    "Raspberry Pi found at: "
  );

  Serial.println(
    MQTT_BROKER
  );

  return true;
}

// MQTT Connection

void reconnectMQTT() {

  while (!client.connected()) {

    Serial.print(
      "Connecting to MQTT broker..."
    );

    String clientID =
        String(DEVICE_ID) +
        "-" +
        WiFi.macAddress();

    if (
      client.connect(
        clientID.c_str()
      )
    ) {

      Serial.println(
        "connected!"
      );

    } else {

      Serial.print(
        "failed, rc="
      );

      Serial.println(
        client.state()
      );

      Serial.println(
        "Retrying in 5 seconds..."
      );

      delay(5000);
    }
  }
}

// Setup

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();

  Serial.println(
    "================================"
  );

  Serial.println(
    " ESP32 AIR & NOISE MONITORING"
  );

  Serial.println(
    " DEVICE: ESP32_02"
  );

  Serial.println(
    " MQ-2 FLAMMABLE GAS SENSOR"
  );

  Serial.println(
    "================================"
  );

  analogReadResolution(12);

  pinMode(
    MQ2_PIN,
    INPUT
  );

  connectWiFi();

  if (!findRaspberryPi()) {

    Serial.println();

    Serial.println(
      "Could not find Raspberry Pi."
    );

    Serial.println(
      "Restarting in 10 seconds..."
    );

    delay(10000);

    ESP.restart();
  }

  client.setBufferSize(1024);

  client.setServer(
    MQTT_BROKER,
    MQTT_PORT
  );

  Serial.println();

  Serial.print(
    "MQTT broker: "
  );

  Serial.println(
    MQTT_BROKER
  );

  Serial.print(
    "MQTT port: "
  );

  Serial.println(
    MQTT_PORT
  );

// INMP441

  i2s_install();

// BME680

  Wire.begin(
    21,
    22
  );

  if (!bme.begin(0x76)) {

    Serial.println(
      "Could not find BME680!"
    );

    while (1) {
      delay(1000);
    }
  }

  bme.setTemperatureOversampling(
    BME680_OS_8X
  );

  bme.setHumidityOversampling(
    BME680_OS_2X
  );

  bme.setPressureOversampling(
    BME680_OS_4X
  );

  bme.setGasHeater(
    320,
    150
  );

  Serial.println(
    "Sensors ready!"
  );

  Serial.println();

  Serial.println(
    "System ready."
  );
}

// Loop

void loop() {

  if (!client.connected()) {
    reconnectMQTT();
  }

  client.loop();

// MQ-2

  float flammableGas2 =
      readMQ2PPM();

// BME680

  if (!bme.performReading()) {

    Serial.println(
      "BME680 reading failed!"
    );

    delay(1000);

    return;
  }

  float temp2 =
      bme.temperature;

  float hum2 =
      bme.humidity;

  float pressure2 =
      (bme.pressure / 100.0) +
      7.6;

  float gas2 =
      bme.gas_resistance / 1000.0;

  float voc2 =
      calculateVOC(gas2) +
      0.45;

  float altitude2 =
      bme.readAltitude(1013.25) -
      16.8;

// INMP441

  float dB2 =
      readSoundLevel();

// Serial

  Serial.println();

  Serial.println(
    "----------- SENSOR DATA -----------"
  );

  Serial.printf(
    "Device: %s\n",
    DEVICE_ID
  );

  Serial.println();

  Serial.println(
    "--- BME680 ---"
  );

  Serial.printf(
    "Temperature: %.2f C\n",
    temp2
  );

  Serial.printf(
    "Humidity: %.2f %%\n",
    hum2
  );

  Serial.printf(
    "Pressure: %.2f hPa\n",
    pressure2
  );

  Serial.printf(
    "Gas: %.2f kOhms\n",
    gas2
  );

  Serial.printf(
    "VOC Concentration: %.2f ppm\n",
    voc2
  );

  Serial.println();

  Serial.println(
    "--- INMP441 ---"
  );

  Serial.printf(
    "Sound: %.2f dB\n",
    dB2
  );

  Serial.printf(
    "Altitude: %.2f m\n",
    altitude2
  );

  Serial.println();

  Serial.println(
    "--- NODE 2 ONLY ---"
  );

  Serial.printf(
    "Flammable Gas: %.2f ppm\n",
    flammableGas2
  );

// JSON

  char payload[1024];

  snprintf(
    payload,
    sizeof(payload),

    "{"
      "\"device_id\":\"%s\","
      "\"temp2\":%.2f,"
      "\"hum2\":%.2f,"
      "\"pressure2\":%.2f,"
      "\"gas2\":%.2f,"
      "\"dB2\":%.2f,"
      "\"altitude2\":%.2f,"
      "\"flammableGas2\":%.2f,"
      "\"voc2\":%.2f"
    "}",

    DEVICE_ID,
    temp2,
    hum2,
    pressure2,
    gas2,
    dB2,
    altitude2,
    flammableGas2,
    voc2
  );

// MQTT Publish

  Serial.println();

  Serial.print(
    "Publishing to: "
  );

  Serial.println(
    MQTT_TOPIC
  );

  Serial.println(
    payload
  );

  if (
    client.publish(
      MQTT_TOPIC,
      payload
    )
  ) {

    Serial.println(
      "MQTT publish successful!"
    );

  } else {

    Serial.println(
      "MQTT publish FAILED!"
    );

    Serial.print(
      "MQTT connected: "
    );

    Serial.println(
      client.connected()
        ? "YES"
        : "NO"
    );

    Serial.print(
      "MQTT state: "
    );

    Serial.println(
      client.state()
    );

    Serial.print(
      "Payload length: "
    );

    Serial.println(
      strlen(payload)
    );
  }

  Serial.println(
    "-----------------------------------"
  );

  delay(5000);
}
