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
const char* DEVICE_ID = "ESP32_01";
const char* MQTT_TOPIC = "esp32/01/sensors";

WiFiClient espClient;
PubSubClient client(espClient);
IPAddress MQTT_BROKER;

// MQ-7

const int MQ7_PIN = 34;
const float VCC = 5.0;
const float RL = 1.0;
const float Ro = 0.542;

// MQ-7 Functions

float readVoltage(int pin) {
  int voltage_mV = analogReadMilliVolts(pin);
  return voltage_mV / 1000.0;
}

float calculateRs(float vout) {
  if (vout <= 0.05 || vout >= VCC) {
    return 999999;
  }

  return RL * (VCC - vout) / vout;
}

float calculatePPM(float rs) {
  float ratio = rs / Ro;
  float ppm = 99.042 * pow(ratio, -1.518);
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

#define I2S_WS 26
#define I2S_SD 33
#define I2S_SCK 25
#define I2S_PORT I2S_NUM_0
#define SAMPLE_RATE 44100
#define BLOCK_SIZE 1024
#define CAL_OFFSET 124.5

int32_t samples[BLOCK_SIZE];

// I2S Setup

void i2s_install() {
  const i2s_config_t i2s_cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 4,
    .dma_buf_len = BLOCK_SIZE,
    .use_apll = false
  };

  const i2s_pin_config_t pin_cfg = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_PORT, &i2s_cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_cfg);
  i2s_zero_dma_buffer(I2S_PORT);
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

  int num_samples = bytes_read / sizeof(int32_t);
  double sum_squares = 0;

  for (int i = 0; i < num_samples; i++) {
    double s = (double)samples[i] / 2147483648.0;
    sum_squares += s * s;
  }

  double rms = sqrt(sum_squares / num_samples);

  if (rms < 1e-8) {
    rms = 1e-8;
  }

  float dB1 = 20.0 * log10(rms) + CAL_OFFSET;

  Serial.printf("Microphone RMS: %.8f\n", rms);

  return dB1;
}

// WiFi Connection

void connectWiFi() {
  Serial.print("Connecting to WiFi");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("ESP32 IP address: ");
  Serial.println(WiFi.localIP());
}

// mDNS

bool findRaspberryPi() {
  Serial.println();
  Serial.println("Starting mDNS...");

  if (!MDNS.begin("esp32-01")) {
    Serial.println("ERROR: mDNS initialization failed!");
    return false;
  }

  Serial.println("mDNS initialized.");
  Serial.println("Searching for Vedang9698.local...");

  IPAddress piIP = MDNS.queryHost(MQTT_HOSTNAME, 5000);

  if (piIP == INADDR_NONE) {
    Serial.println("ERROR: Raspberry Pi not found!");
    return false;
  }

  MQTT_BROKER = piIP;

  Serial.print("Raspberry Pi found at: ");
  Serial.println(MQTT_BROKER);

  return true;
}

// MQTT Connection

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT broker...");

    String clientID = String(DEVICE_ID) + "-" + WiFi.macAddress();

    if (client.connect(clientID.c_str())) {
      Serial.println("connected!");
    } else {
      Serial.print("failed, rc=");
      Serial.println(client.state());
      Serial.println("Retrying in 5 seconds...");
      delay(5000);
    }
  }
}

// Setup

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println(" ESP32 AIR & NOISE MONITORING");
  Serial.println(" DEVICE: ESP32_01");
  Serial.println(" MQ-7 CARBON MONOXIDE SENSOR");
  Serial.println("================================");

  analogReadResolution(12);
  analogSetPinAttenuation(MQ7_PIN, ADC_11db);

  connectWiFi();

  if (!findRaspberryPi()) {
    Serial.println();
    Serial.println("Could not find Raspberry Pi.");
    Serial.println("Restarting in 10 seconds...");
    delay(10000);
    ESP.restart();
  }

  client.setBufferSize(1024);
  client.setServer(MQTT_BROKER, MQTT_PORT);

  Serial.println();
  Serial.print("MQTT broker: ");
  Serial.println(MQTT_BROKER);
  Serial.print("MQTT port: ");
  Serial.println(MQTT_PORT);

  i2s_install();

  Wire.begin(21, 22);

  if (!bme.begin(0x76)) {
    Serial.println("Could not find BME680!");

    while (1) {
      delay(1000);
    }
  }

  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setGasHeater(320, 150);

  Serial.println("Sensors ready!");
  Serial.println();
  Serial.println("System ready.");
}

// Loop

void loop() {
  if (!client.connected()) {
    reconnectMQTT();
  }

  client.loop();

  float mq7Voltage1 = readVoltage(MQ7_PIN);
  float mq7Rs1 = calculateRs(mq7Voltage1);
  float co1 = calculatePPM(mq7Rs1);

  if (!bme.performReading()) {
    Serial.println("BME680 reading failed!");
    delay(1000);
    return;
  }

  float temp1 = bme.temperature;
  float hum1 = bme.humidity;
  float pressure1 = (bme.pressure / 100.0)+6.6;
  float gas1 = bme.gas_resistance / 1000.0;
  float voc1 = calculateVOC(gas1);

  float altitude1 = bme.readAltitude(1013.25)-8.55;

  float dB1 = readSoundLevel();

  Serial.println();
  Serial.println("----------- SENSOR DATA -----------");
  Serial.printf("Device: %s\n", DEVICE_ID);

  Serial.println();
  Serial.println("--- BME680 ---");
  Serial.printf("Temperature: %.2f C\n", temp1);
  Serial.printf("Humidity: %.2f %%\n", hum1);
  Serial.printf("Pressure: %.2f hPa\n", pressure1);
  Serial.printf("Gas: %.2f kOhms\n", gas1);
  Serial.printf("VOC Concentration: %.2f ppm\n", voc1);

  Serial.println();
  Serial.println("--- INMP441 ---");
  Serial.printf("Sound: %.2f dB\n", dB1);
  Serial.printf("Altitude: %.2f m\n", altitude1);

  Serial.println();
  Serial.println("--- NODE 1 ONLY ---");
  Serial.printf("CO: %.2f ppm\n", co1);
  Serial.printf("VOC Concentration: %.2f ppm\n", voc1);

// JSON

  char payload[1024];

  snprintf(
    payload,
    sizeof(payload),
    "{"
    "\"device_id\":\"%s\","
    "\"temp1\":%.2f,"
    "\"hum1\":%.2f,"
    "\"pressure1\":%.2f,"
    "\"gas1\":%.2f,"
    "\"dB1\":%.2f,"
    "\"altitude1\":%.2f,"
    "\"co1\":%.2f,"
    "\"voc1\":%.2f"
    "}",
    DEVICE_ID,
    temp1,
    hum1,
    pressure1,
    gas1,
    dB1,
    altitude1,
    co1,
    voc1
  );

// MQTT Publish

  Serial.println();
  Serial.print("Publishing to: ");
  Serial.println(MQTT_TOPIC);
  Serial.println(payload);

  if (client.publish(MQTT_TOPIC, payload)) {
    Serial.println("MQTT publish successful!");
  } else {
    Serial.println("MQTT publish FAILED!");

    Serial.print("MQTT connected: ");
    Serial.println(client.connected() ? "YES" : "NO");

    Serial.print("MQTT state: ");
    Serial.println(client.state());

    Serial.print("Payload length: ");
    Serial.println(strlen(payload));
  }

  Serial.println("-----------------------------------");

  delay(5000);
}
