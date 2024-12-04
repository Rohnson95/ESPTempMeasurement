#include "secrets.h"
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>
#include "WiFi.h"
#include "DHT.h"



// Function Prototypes
void messageHandler(String &topic, String &payload);
void updateSettings(JsonVariant settingsObj);

// The MQTT topics that this device should publish/subscribe
#define AWS_IOT_PUBLISH_TOPIC "/telemetry"
#define AWS_IOT_SUBSCRIBE_TOPIC "/downlink"

long sendInterval = 10000;  // interval at which to send to AWS

String THINGNAME = "";

WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(1024);

void connectAWS() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // get the macAddress
  THINGNAME = WiFi.macAddress();
  for (int i = 0; i < THINGNAME.length(); i++) {
    if (THINGNAME.charAt(i) == ':') {
      THINGNAME.remove(i, 1);
      i--;
    }
  }

  Serial.println("MAC Address: " + THINGNAME);

  Serial.println("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  client.begin(AWS_IOT_ENDPOINT, 8883, net);
  client.onMessage(messageHandler);

  Serial.println("Connecting to AWS IoT");
  while (!client.connect(THINGNAME.c_str())) {
    Serial.print(".");
    delay(100);
  }

  if (!client.connected()) {
    Serial.println("AWS IoT Timeout!");
    return;
  }

  client.subscribe(THINGNAME + AWS_IOT_SUBSCRIBE_TOPIC);
  Serial.println("AWS IoT Connected!");
}

void setupShadow() {
  client.subscribe("$aws/things/" + THINGNAME + "/shadow/get/accepted");
  client.subscribe("$aws/things/" + THINGNAME + "/shadow/get/rejected");
  client.subscribe("$aws/things/" + THINGNAME + "/shadow/update/delta");
  client.publish("$aws/things/" + THINGNAME + "/shadow/get");
}

bool publishTelemetry(String payload) {
  Serial.println("Publishing: " + payload);
  return client.publish(THINGNAME + AWS_IOT_PUBLISH_TOPIC, payload);
}

void messageHandler(String &topic, String &payload) {
  Serial.println("incoming: " + topic + " - " + payload);

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.println("Failed to parse JSON payload!");
    return;
  }

  if (topic.endsWith("/shadow/get/accepted")) {
    updateSettings(doc["state"]["desired"]);
  } else if (topic.endsWith("/shadow/update/delta")) {
    updateSettings(doc["state"]);
  }
}

void updateSettings(JsonVariant settingsObj) {
  if (settingsObj.isNull()) {
    Serial.println("Invalid settings object!");
    return;
  }

  // Hämta och validera "sendIntervalSeconds" från Shadow
  int newInterval = settingsObj["sendIntervalSeconds"].as<int>();
  if (newInterval > 0) {
    sendInterval = newInterval * 1000;  // Konvertera till millisekunder
    Serial.println("sendInterval updated to: " + String(sendInterval) + " ms");
  } else {
    Serial.println("Invalid sendInterval received, keeping default.");
    sendInterval = 10000;  // Fallback till standardvärde
  }

  // Rapportera uppdaterad state till Shadow
  DynamicJsonDocument docResponse(512);
  docResponse["state"]["reported"] = settingsObj;

  char jsonBuffer[512];
  serializeJson(docResponse, jsonBuffer);

  Serial.print("Sending reported state to AWS: ");
  serializeJson(docResponse, Serial);

  client.publish("$aws/things/" + THINGNAME + "/shadow/update", jsonBuffer);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  connectAWS();
  setupShadow();
}

void loop() {
  static unsigned long previousMillis = -sendInterval;

  client.loop();

  if (millis() - previousMillis >= sendInterval) {
    previousMillis = millis();

    bool sendResult = publishTelemetry("{\"temperature\":" + String(random(15.0, 30.0)) + ",\"humidity\":" + String(random(50, 90)) + "}");
    if (sendResult == 0) {
      ESP.restart();
    }
  }
}
