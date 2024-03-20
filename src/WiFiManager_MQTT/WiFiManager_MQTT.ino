#include <FS.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>


#define TRIGGER_PIN 0


bool shouldSaveConfig = false;
#define mqtt_server "www.example.com"
#define mqtt_port "1883"
#define mqtt_user "mqtt Username"
#define mqtt_pass "mqtt Password"
#define humidity_topic "sensor/humidity"

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

  Serial.print("Custom MQTT Username: ");
  Serial.println(custom_mqtt_user.getValue());

  if (!wifiManager.autoConnect("AutoConnectAP", "password")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    ESP.reset();
    delay(5000);
  }

  Serial.println("connected...yeey :)");
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

  Serial.println("local ip");
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
    if (client.connect("ESP8266Client", mqtt_user, mqtt_pass)) {
      Serial.println("connected");
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

long lastMsg = 0;
float temp = 0.0;
float hum = 0.0;
float diff = 1.0;

void loop() {
  checkButton();
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  long now = millis();
  if (now - lastMsg > 1000) {
    lastMsg = now;

    float newTemp = 10;
    float newHum = 20;

    if (checkBound(newHum, hum, diff)) {
      hum = newHum;
      Serial.print("New humidity:");
      Serial.println(String(hum).c_str());
      client.publish(humidity_topic, String(hum).c_str(), true);
    }
  }
  // Include the repetitive program here
}