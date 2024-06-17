#include <FS.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>


#define TRIGGER_PIN 0


bool shouldSaveConfig = false;
#define mqtt_server "www.example.com"
#define mqtt_port "1883"
#define mqtt_user "mqtt Username"
#define mqtt_pass "mqtt Password"

int mqttPortInt = 1883;

WiFiClient espClient;
PubSubClient client(espClient);
WiFiManager wifiManager;

void saveConfigCallback() {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  pinMode(TRIGGER_PIN, INPUT);
  Serial.println("mounting FS...");
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        DynamicJsonDocument jsonBuffer(2048);
        DeserializationError error = deserializeJson(jsonBuffer, buf.get());
        if (!error) {
          Serial.println("parsed json");
          strcpy(mqtt_server, jsonBuffer["mqtt_server"]);
          mqttPortInt = jsonBuffer["mqtt_port"];
          strcpy(mqtt_user, jsonBuffer["mqtt_user"]);
          strcpy(mqtt_pass, jsonBuffer["mqtt_pass"]);
          Serial.println("data copied");

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }

  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 30);
  WiFiManagerParameter custom_mqtt_pass("pass", "mqtt pass", mqtt_pass, 20);


  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_pass);


  wifiManager.setConnectTimeout(20);
  wifiManager.setConfigPortalTimeout(30);
  wifiManager.setAPClientCheck(true);
  wifiManager.setHostname("Your device name");  // Enter your device name here

  bool res = wifiManager.autoConnect("GreenHouse AP", "Isuru234");
  if (!res) {
    Serial.println("failed to connect and hit timeout");
    ESP.reset();
  } else {
    Serial.println("connected...yeey :)");
  }

  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_pass, custom_mqtt_pass.getValue());

  if (shouldSaveConfig) {
    Serial.println("saving config");
    mqttPortInt = atoi(mqtt_port);
    DynamicJsonDocument jsonBuffer(2048);
    JsonObject json = jsonBuffer.to<JsonObject>();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqttPortInt;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_pass"] = mqtt_pass;

    String jsonString;
    serializeJson(jsonBuffer, jsonString);

    Serial.println(jsonString);
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }
    configFile.println(jsonString);
    configFile.close();
  }
  ArduinoOTA.setHostname("Your OTA Host name");
  ArduinoOTA.setPassword("password for OTA");
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else  // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready OTA8");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void checkButton() {
  // check for button press
  if (digitalRead(TRIGGER_PIN) == LOW) {
    // poor mans debounce/press-hold, code not ideal for production
    delay(50);
    if (digitalRead(TRIGGER_PIN) == LOW) {
      Serial.println("Button Pressed");
      // still holding button for 3000 ms, reset settings, code not ideaa for production
      delay(3000);  // reset delay hold
      if (digitalRead(TRIGGER_PIN) == LOW) {
        Serial.println("Button Held");
        Serial.println("Erasing Config, restarting");
        wifiManager.resetSettings();
        if (SPIFFS.exists("/config.json")) {
          // Delete the /config.json file
          SPIFFS.remove("/config.json");
          Serial.println("/config.json file deleted.");
        } else {
          Serial.println("/config.json file does not exist.");
        }
        ESP.restart();
      }

      // start portal w delay
      Serial.println("Starting config portal");
      wifiManager.setConfigPortalTimeout(120);

      if (!wifiManager.startConfigPortal("OnDemandAP", "password")) {
        Serial.println("failed to connect or hit timeout");
        delay(3000);
        // ESP.restart();
      } else {
        //if you get here you have connected to the WiFi
        Serial.println("connected...yeey :)");
      }
    }
  }
}

void MQTTsubscribe();

unsigned long lastReconnectAttempt = 0;


void reconnect() {
  const unsigned long reconnectInterval = 5000;  // 5 seconds

  // If connected, return immediately
  if (client.connected()) {
    return;
  }
  // Check for millis() rollover
  if (millis() < lastReconnectAttempt) {
    lastReconnectAttempt = millis();
  }
  // Try to reconnect at the specified interval
  if (millis() - lastReconnectAttempt > reconnectInterval) {
    lastReconnectAttempt = millis();
    checkButton();
    Serial.print("Attempting MQTT connection...");
    client.setServer(mqtt_server, mqttPortInt);
    client.setCallback(callBack);
    if (client.connect("ESP8266Client", mqtt_user, mqtt_pass)) {
      Serial.println("connected");
      MQTTsubscribe();
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again later");
    }
  }

  // Perform software reset if millis() has rolled over
  if (millis() < lastReconnectAttempt) {
    Serial.println("millis() rollover detected, performing software reset");
    ESP.restart();
  }
}

bool checkBound(float newValue, float prevValue, float maxDiff) {
  return !isnan(newValue) && (newValue < prevValue - maxDiff || newValue > prevValue + maxDiff);
}


void loop() {
  checkButton();
  ArduinoOTA.handle();
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  // Include the repetitive program here
}