#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#define TFT_BL 21

const char *ssid = "sdsmtopn";
const char *password = "";

void setup() {
  Serial.begin(115200);
  Serial.println("Booting...");

  // Initialize the Backlight
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Connect to the WiFi network
  Serial.printf("Connecting to WiFi SSID: %s...\n", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Wait for connection (blink backlight while connecting)
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 30) {
    digitalWrite(TFT_BL, LOW);
    delay(250);
    digitalWrite(TFT_BL, HIGH);
    delay(250);
    Serial.print(".");
    attempt++;
  }
  Serial.println("");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Start mDNS
    if (!MDNS.begin("peachpulp")) {
      Serial.println("Error setting up MDNS responder!");
    } else {
      Serial.println("mDNS responder started: peachpulp.local");
    }
  } else {
    Serial.println("WiFi connection failed!");
  }

  // Set the OTA Hostname
  ArduinoOTA.setHostname("peachpulp");

  // Configure the OTA Event Callbacks
  ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
          type = "sketch";
        } else { // U_SPIFFS
          type = "filesystem";
        }
        Serial.println("Start updating " + type);
      })
      .onEnd([]() { Serial.println("\nEnd"); })
      .onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
          Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
          Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
          Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
          Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR)
          Serial.println("End Failed");
      });

  // Start the OTA Listener
  ArduinoOTA.begin();

  // Success Blink
  for (int i = 0; i < 3; i++) {
    digitalWrite(TFT_BL, LOW);
    delay(150);
    digitalWrite(TFT_BL, HIGH);
    delay(400);
  }
}

void loop() { ArduinoOTA.handle(); }