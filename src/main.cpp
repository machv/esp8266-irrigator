#include <FS.h> //this needs to be first
#include <Arduino.h>
#include <WiFiManager.h> 
#include <ESP8266WebServer.h>
#include <PubSubClient.h>         //MQTT server library
#include <EasyButton.h>
#include <ArduinoJson.h>

//for LED status
#include <Ticker.h>
Ticker ticker;

struct Config {
  String mqtt_server;
  int mqtt_port;
  String mqtt_user;
  String mqtt_password;
  String mqtt_channel;
};
const char *ConfigFileName = "/config.json";
Config config; 
String mqttTopicRelay1;
String mqttTopicRelay2;

ESP8266WebServer server(80);

// MQTT
WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastMqttConnectionRetryTime;
unsigned long mqttReconnectDelay = 30000;

#define PinMeter1 D5
#define PinMeter2 D6
#define PinRelay1 D2
#define PinRelay2 D3
#define PinButton1 D5
#define PinButton2 D6
#define PinLed1 D0
#define PinLed2 D1

bool relay1state = false;
bool relay2state = false;

EasyButton button1(PinButton1);
EasyButton button2(PinButton2);

// The hall-effect flow sensor outputs approximately 4.5 pulses per second per
// litre/minute of flow.
float calibrationFactor = 4.5;

// To store the "rise ups" from the flow meter pulses
volatile byte pulseCounterMeter1 = 0;
volatile byte pulseCounterMeter2 = 0;

float flowRate1;
float flowRate2;

unsigned int flowMilliLitres1;
unsigned int flowMilliLitres2;

unsigned long totalMilliLitres1;
unsigned long totalMilliLitres2;

unsigned long oldTime;

//Interrupt function, so that the counting of pulse “rise ups” dont interfere with the rest of the code  (attachInterrupt)
void meter1_triggered() {
  pulseCounterMeter1++;

  Serial.printf("Trigger 1, current count = %d", pulseCounterMeter1);
  Serial.println();
}
void meter2_triggered() {
  pulseCounterMeter2++;

  Serial.printf("Trigger 2, current count = %d", pulseCounterMeter2);
  Serial.println();
}

void tick() {
  //toggle state
  //int state = digitalRead(LED_BUILTIN);  // get the current state of GPIO1 pin
  //digitalWrite(LED_BUILTIN, !state);     // set pin to the opposite state

  Serial.println("tick");
}

void toggleRelay(int id) {
  uint8_t relayPin = 0;
  uint8_t ledPin = 0;
  String channel;
  
  switch(id) {
    case 1:
      relayPin = PinRelay1;
      ledPin = PinLed1;
      channel = mqttTopicRelay1;
      break;
    case 2:
      relayPin = PinRelay2;
      ledPin = PinLed2;
      channel = mqttTopicRelay2;
      break;
    default:
      return;
      break;
  }

  int currentValue = digitalRead(relayPin);
  int newValue = !currentValue;

  Serial.printf("Toggling relay #%d from %i to %i", id, currentValue, newValue);

  // Do the toggle
  // HIGH (0x1) = OFF, LOW (0x0) = ON
  digitalWrite(relayPin, newValue); // relay
  digitalWrite(ledPin, !newValue); // led
  
  if(id == 1) {
    relay1state = !newValue;
  } else if (id == 2) {
    relay2state = !newValue;
  }

  Serial.println();

  // And publish MQTT update
  if(mqttClient.connected()) {
    Serial.println("[MQTT] Publishing updated state after toggle.");

    String value = String(!newValue); // actual state is negation of the PIN state
    mqttClient.publish(channel.c_str(), value.c_str());
  }
}

void mqttSubscriptionCallback(char* topic, byte* payload, unsigned int length) {
  // report to terminal for debug
  Serial.print("[MQTT] Received message in topic '");
  Serial.print(topic);
  Serial.print("' with content: ");
  for (uint i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // check if we received topics for state change 
  if (strcmp (mqttTopicRelay1.c_str(), topic) == 0) {
    Serial.printf("Relay 1 state change requested to %c", payload[0]);
    if(
        (payload[0] == '1' && relay1state == false) ||
        (payload[0] == '0' && relay1state == true)
      ) {
        Serial.print(", current state ");
        Serial.print(relay1state);
        Serial.println(" differs -> toggle");
        toggleRelay(1);
    } else {
      Serial.println();
    }
  } else if (strcmp (mqttTopicRelay2.c_str(), topic) == 0) {
    Serial.print("Relay 2 state change requested");
    if(
        (payload[0] == '1' && relay2state == false) ||
        (payload[0] == '0' && relay2state == true)
      ) {
           Serial.print(", current state ");
        Serial.print(relay2state);
        Serial.println(" differs -> toggle");
          toggleRelay(2);
    } else {
      Serial.println();
    }
  }  else {
    Serial.println("[MQTT] No match for any action.");
  }
}

void setupMqtt(int retries) {  
  if(mqttClient.connected()) {
    Serial.println("[MQTT] Disconnecting...");
    mqttClient.disconnect();
  }

  if(config.mqtt_server.length() == 0) {
    return; // no server, no connection needed
  }
  
  mqttTopicRelay1 = String(config.mqtt_channel + "1/state");
  mqttTopicRelay2 = String(config.mqtt_channel + "2/state");

  mqttClient.setServer(config.mqtt_server.c_str(), config.mqtt_port);
  mqttClient.setCallback(mqttSubscriptionCallback);

  int retryCount = 0;
  while (!mqttClient.connected() && retryCount < retries) {
      Serial.print("[MQTT] Connecting (retry #");
      Serial.print(retryCount + 1);
      Serial.println(")...");
  
      bool connectionState = false;
      if(config.mqtt_user.length() > 0) {
        connectionState = mqttClient.connect("Zavlazovac", config.mqtt_user.c_str(), config.mqtt_password.c_str());
      } else {
        connectionState = mqttClient.connect("Zavlazovac");
      }
      
      if (connectionState) {
        Serial.println("[MQTT] Connected successfully.");  
      } else {
        Serial.print("[MQTT] Connection failed with code: ");
        Serial.println(mqttClient.state());
        delay(2000);
      }

      retryCount++;
  }

  // And subscribe to respective channels
  if(mqttClient.connected()) {
    Serial.println("[MQTT] Publishing current states of relays.");
    String value1 = String(relay1state);
    mqttClient.publish(mqttTopicRelay1.c_str(), value1.c_str());
    String value2 = String(relay2state);
    mqttClient.publish(mqttTopicRelay2.c_str(), value2.c_str());

    Serial.println("[MQTT] Subscribing to the channels:");
    Serial.print("  - ");
    Serial.println(mqttTopicRelay1);
    mqttClient.subscribe(mqttTopicRelay1.c_str());
    
    Serial.print("  - ");
    Serial.println(mqttTopicRelay2);
    mqttClient.subscribe(mqttTopicRelay2.c_str());
  }

  lastMqttConnectionRetryTime = millis();
}

void connectMqtt() {
  setupMqtt(5); // By default try 5-times before gave up
}

// Gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  
  //entered config mode, make led toggle faster
  //ticker.attach(0.2, tick);
}

String SendSettingsHTML(){
  Serial.println("sending html");

  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<title>LED Control</title>\n";
  ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n";
  ptr +=".button {display: block;background-color: #1abc9c;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 4px;}\n";
  ptr +=".button-on {background-color: #1abc9c;}\n";
  ptr +=".button-on:active {background-color: #16a085;}\n";
  ptr +=".button-off {background-color: #34495e;}\n";
  ptr +=".button-off:active {background-color: #2c3e50;}\n";
  ptr += "th {text-align: left; font-size: 12pt}";
  ptr += "input { padding: 5px; font-size: 12pt}";
  ptr +="table {margin: 0 auto}\n";
  ptr +="p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body><div class=\"page\">\n";
  ptr +="<h1>Zavlazovac</h1>\n";
  ptr +="<h3>Settings</h3>\n";
  
  if(server.hasHeader("X-Config-Saved")) {
    ptr += "<div style=\"text-align: center; margin: 20px; font-weight: bold\">Configuration saved.</div>";
  }

  ptr +="<form method=\"post\" enctype=\"application/x-www-form-urlencoded\">\n";
  ptr +="<table>\n"
  "  <tr>\n"
  "    <th>MQTT Server</th>\n"
  "    <td><input type=\"text\" name=\"mqtt_server\" value=\"" + (config.mqtt_server) + "\"></td>\n"
  "  </tr>\n"

  "  <tr>\n"
  "    <th>MQTT Port</th>\n"
  "    <td><input type=\"text\" name=\"mqtt_port\" value=\"" + (config.mqtt_port) + "\"></td>\n"
  "  </tr>\n"

  "  <tr>\n"
  "    <th>MQTT User</th>\n"
  "    <td><input type=\"text\" name=\"mqtt_user\" value=\"" + (config.mqtt_user) + "\"></td>\n"
  "  </tr>\n"

    "  <tr>\n"
  "    <th>MQTT Password</th>\n"
  "    <td><input type=\"password\" name=\"mqtt_password\" value=\"" + (config.mqtt_password) + "\"></td>\n"
  "  </tr>\n"

  "  <tr>\n"
  "    <th>MQTT Channel</th>\n"
  "    <td><input type=\"text\" name=\"mqtt_channel\" value=\"" + (config.mqtt_channel) + "\"></td>\n"
  "  </tr>\n"

  "  <tr>\n"
  "    <td colspan=\"2\"><input class=\"button\" type=\"submit\" value=\"Save Changes\"></td>\n"
  "  </tr>\n"
  "</table>\n";
  
  ptr += "<div style=\"text-align: center\">"
          "<a href=\"/\" class=\"button\">Back</a>"
          "</div>";

  ptr +="</form>\n"
        "</body>\n"
        "</html>\n";
  
  return ptr;
}

String SendHTML(){
  Serial.println("sending html");

  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<title>LED Control</title>\n";
  ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n";
  ptr +=".button {display: block;background-color: #1abc9c;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 4px;}\n";
  ptr +=".button-on {background-color: #1abc9c;}\n";
  ptr +=".button-on:active {background-color: #16a085;}\n";
  ptr +=".button-off {background-color: #34495e;}\n";
  ptr +=".button-off:active {background-color: #2c3e50;}\n";
  ptr +=".page{margin-left: 20px; margin-right: 20px}\n";
  ptr +="p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body>\n<div class=\"page\">";
  ptr +="<h1>Zavlazovac</h1>\n";
  
  ptr += "<div>";
  if(relay1state == true) {
    ptr += "<a class=\"button button-on\" href=\"/toggle?id=1\">Turn OFF relay 1</a>";
  } else {
    ptr += "<a class=\"button button-off\" href=\"/toggle?id=1\">Turn ON relay 1</a>";
  }
  ptr += "</div>";

  ptr += "<div>";
  if(relay2state == true) {
    ptr += "<a class=\"button button-on\" href=\"/toggle?id=2\">Turn OFF relay 2</a>";
  } else {
    ptr += "<a class=\"button button-off\" href=\"/toggle?id=2\">Turn ON relay 2</a>";
  }
  ptr += "</div>";

  ptr +="<hr>\n";
  ptr +="<a class=\"button\" href=\"/config\">Configuration</a>\n";
  ptr +="</div></body>\n";
  ptr +="</html>\n";
  return ptr;
}

void handle_pageConfig() {
  server.send(200, "text/html", SendSettingsHTML()); 
}

void handle_saveConfig() {
  // Save runtime data
  String arg = server.arg("mqtt_port");
  arg.trim();
  config.mqtt_port = (arg.length() == 0 ? 1883 : arg.toInt());
  config.mqtt_server = server.arg("mqtt_server");
  config.mqtt_user = server.arg("mqtt_user"); 
  config.mqtt_password = server.arg("mqtt_password");
  
  String channel = server.arg("mqtt_channel");
  channel.trim();
  if(!channel.endsWith("/"))
    channel += "/";
  config.mqtt_channel = channel;

  Serial.println(config.mqtt_server);

  File configFile = SPIFFS.open(ConfigFileName, "w");

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/assistant to compute the capacity.
  StaticJsonDocument<512> jsonDocument;

  // Set the values in the document
  jsonDocument["mqtt_server"] = config.mqtt_server;
  jsonDocument["mqtt_port"] = config.mqtt_port;
  jsonDocument["mqtt_user"] = config.mqtt_user;
  jsonDocument["mqtt_password"] = config.mqtt_password;
  jsonDocument["mqtt_channel"] = config.mqtt_channel;

  // Serialize JSON to file
  if (serializeJson(jsonDocument, configFile) == 0) {
    Serial.println(F("Failed to write to file"));
  }

  // Close the file
  configFile.close();

  // Reconnect MQTT to reflect changes
  connectMqtt();

  server.sendHeader("Location", "/config?saved=1", true);
  server.send(303, "text/plain"); 
}

void handle_OnConnect() {
  server.send(200, "text/html", SendHTML()); 
}

void handle_toggle() {
  if(!server.hasArg("id")) {
    server.send(400, "text/html", "Missing required id parameter.");
    return;
  }

  if(server.arg("id") == "1")
    toggleRelay(1);

  if(server.arg("id") == "2")
    toggleRelay(2);

  server.sendHeader("Location", "/");
  server.send(303, "text/plain");
}

void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}

// Callback function to be called when the button is pressed.
void onPressed1() {
	Serial.println("Button 1 has been pressed!");
  toggleRelay(1);
}

void onPressed2() {
	Serial.println("Button 2 has been pressed!");
  toggleRelay(2);
}

void setup() {
  // For testing
  //SPIFFS.format();
  //WiFiManager.reset();

  // Set serial console Baud ate
  Serial.begin(115200);

  // Initialize the button.
	button1.begin();
  button1.onPressed(onPressed1);
  
  button2.begin();
  button2.onPressed(onPressed2);
	
  // Initialize GPIO PINs
  pinMode(PinLed1, OUTPUT);
  pinMode(PinLed2, OUTPUT);
  pinMode(PinRelay1, OUTPUT);
  digitalWrite(PinRelay1, HIGH); // by default turn it off (=HIGH)
  pinMode(PinRelay2, OUTPUT);
  digitalWrite(PinRelay2, HIGH); // by default turn it off (=HIGH)
  pinMode(PinMeter1, INPUT);
  pinMode(PinMeter2, INPUT);
  //pinMode(PinButton1, INPUT); // buttons handled by EasyButton
  //pinMode(PinButton2, INPUT); // buttons handled by EasyButton
  
  // And attach interrupt watches to meter PINs
  attachInterrupt(digitalPinToInterrupt(PinMeter1), meter1_triggered, FALLING);
  attachInterrupt(digitalPinToInterrupt(PinMeter2), meter2_triggered, FALLING);

  // !!! using internal LED blocks internal TTY output !!! On-board LED je připojena mezi TX1 = GPIO2 a VCC 
   //set led pin as output
  //pinMode(LED_BUILTIN, OUTPUT);
  // start ticker with 0.5 because we start in AP mode and try to connect
  //ticker.attach(0.6, tick);

  // WiFiManager
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(120);
  
  if(!wifiManager.autoConnect("Zavlazovac-Setup")) {
      Serial.println("failed to connect and hit timeout");
      delay(3000);

      // Reset and try again, or maybe put it to deep sleep
      ESP.reset();
      delay(5000);
  } 

  // Set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  //if you get here you have connected to the WiFi
  
  //ticker.detach();
  //keep LED on
  //digitalWrite(LED_BUILTIN, LOW);

  // init values from file system
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");

    // read config file
    if(SPIFFS.exists(ConfigFileName))
    {
      Serial.println("Reading existing configuration.");

      File configFile = SPIFFS.open(ConfigFileName, "r");

      StaticJsonDocument<512> json;
      DeserializationError error = deserializeJson(json, configFile);
      if (error)
        Serial.println(F("Failed to read file, using default configuration"));

        // Copy values from the JsonDocument to the Config
        config.mqtt_port = json["mqtt_port"] | 1883;
        config.mqtt_server = json["mqtt_server"].as<String>();
        config.mqtt_user = json["mqtt_user"].as<String>();
        config.mqtt_password = json["mqtt_password"].as<String>();
        config.mqtt_channel = json["mqtt_channel"].as<String>();

        configFile.close();
    }
  }

  // setup MQTT
  connectMqtt();

  // Web server pages
  server.on("/", handle_OnConnect);
  server.on("/config", HTTP_GET, handle_pageConfig);
  server.on("/config", HTTP_POST, handle_saveConfig);
  server.on("/toggle", handle_toggle);
  server.onNotFound(handle_NotFound);  
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  // Process web server requests
  server.handleClient();
  
  // MQTT reconnect if no connection with delay
  if(!mqttClient.connected() && ((millis() - lastMqttConnectionRetryTime) > mqttReconnectDelay)) {
    setupMqtt(1);
  }
  
  // Process MQTT communication
  mqttClient.loop();
  
  // Continuously read the status of the button. 
	button1.read();
  button2.read();
/*
  // Process water flow sensors
  if((millis() - oldTime) > 1000) // Only process counters once per second
  {
    // Disable the interrupt while calculating flow rate and sending the value to the host
    detachInterrupt(PinMeter1);
    detachInterrupt(PinMeter2);

    // Because this loop may not complete in exactly 1 second intervals we calculate
    // the number of milliseconds that have passed since the last execution and use
    // that to scale the output. We also apply the calibrationFactor to scale the output
    // based on the number of pulses per second per units of measure (litres/minute in
    // this case) coming from the sensor.
    flowRate1 = ((1000.0 / (millis() - oldTime)) * pulseCounterMeter1) / calibrationFactor;
    flowRate2 = ((1000.0 / (millis() - oldTime)) * pulseCounterMeter1) / calibrationFactor;

    // Note the time this processing pass was executed. Note that because we've
    // disabled interrupts the millis() function won't actually be incrementing right
    // at this point, but it will still return the value it was set to just before
    // interrupts went away.
    oldTime = millis();

    // Divide the flow rate in litres/minute by 60 to determine how many litres have
    // passed through the sensor in this 1 second interval, then multiply by 1000 to
    // convert to millilitres.
    flowMilliLitres1 = (flowRate1 / 60) * 1000;
    flowMilliLitres2 = (flowRate2 / 60) * 1000;

    // Add the millilitres passed in this second to the cumulative total
    totalMilliLitres1 += flowMilliLitres1;
    totalMilliLitres2 += flowMilliLitres2;

    unsigned int frac;

    // Print the flow rate for this second in litres / minute
    Serial.print("[Valve 1] Flow rate: ");
    Serial.print(int(flowRate1)); // Print the integer part of the variable
    Serial.print("."); // Print the decimal point
    // Determine the fractional part. The 10 multiplier gives us 1 decimal place.
    frac = (flowRate1 - int(flowRate1)) * 10;
    Serial.print(frac, DEC); // Print the fractional part of the variable
    Serial.print("L/min");
    // Print the number of litres flowed in this second
    Serial.print("  Current Liquid Flowing: "); // Output separator
    Serial.print(flowMilliLitres1);
    Serial.print("mL/Sec");

    // Print the cumulative total of litres flowed since starting
    Serial.print("  Output Liquid Quantity: "); // Output separator
    Serial.print(totalMilliLitres1);
    Serial.println("mL");

    // Print the flow rate for this second in litres / minute
    Serial.print("[Valve 2] Flow rate: ");
    Serial.print(int(flowRate2)); // Print the integer part of the variable
    Serial.print("."); // Print the decimal point
    // Determine the fractional part. The 10 multiplier gives us 1 decimal place.
    frac = (flowRate2 - int(flowRate2)) * 10;
    Serial.print(frac, DEC); // Print the fractional part of the variable
    Serial.print("L/min");
    // Print the number of litres flowed in this second
    Serial.print("  Current Liquid Flowing: "); // Output separator
    Serial.print(flowMilliLitres2);
    Serial.print("mL/Sec");

    // Print the cumulative total of litres flowed since starting
    Serial.print("  Output Liquid Quantity: "); // Output separator
    Serial.print(totalMilliLitres2);
    Serial.println("mL");

    if(mqttClient.connected()) {
      if(flowRate1 > 0) {
        String channel = String(config.mqtt_channel + "1/flow");
        String value = String(flowRate1);
        mqttClient.publish(channel.c_str(), value.c_str());
      }

      if(flowRate2 > 0) {
        String channel = String(config.mqtt_channel + "2/flow");
        String value = String(flowRate2);
        mqttClient.publish(channel.c_str(), value.c_str());
      }
    }

    // Reset the pulse counter so we can start incrementing again
    pulseCounterMeter1 = 0;
    pulseCounterMeter2 = 0;

    // Enable the interrupt again now that we've finished sending output
    attachInterrupt(digitalPinToInterrupt(PinMeter1), meter1_triggered, FALLING);
    attachInterrupt(digitalPinToInterrupt(PinMeter2), meter2_triggered, FALLING);
  }
  */
}
