void MQTTsubscribe() {
  /*your MQTT topics here
  client.subscribe("topic");
  */
}

void callBack(char* topic, byte* payload, unsigned int length) {
/*
    Received data from MQTT will be processed here.
    Template for JSON strings:

    Example topic: example
    Example payload:

    {
      "device": "ESP8266",
      "State": 1
    }
    */

  // This line indicates the receipt of a message. (Topic should be subscribed.)
  Serial.println("Message arrived in topic: " + String(topic));

  // Allocate a buffer to store the incoming payload
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';

  DynamicJsonDocument jsonBuffer(512);
  DeserializationError error = deserializeJson(jsonBuffer, message);

  // reject the payload if there is a issue in deserialization
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.f_str());
    return;
  }

  // Check if the topic is matched and if the required keys are available, or reject.

  if (String(topic) == "example") {
    if (!jsonBuffer.containsKey("device") || !jsonBuffer.containsKey("State")) {
      return;
    }
    // save each key data to variables
    const char* device = jsonBuffer["device"];
    bool state = jsonBuffer["State"];

    if (strcmp(device, "ESP8266") == 0) {
      // do something if the device is "ESP8266" (check the example payload on line 10 in mqtt.ino)
    }
    if (state) {
      //Do something if state is equals to 1 (check the example payload on line 11 in mqtt.ino)
    }
  }
}