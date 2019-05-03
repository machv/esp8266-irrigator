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

EasyButton button(D3);

ESP8266WebServer server(80);

WiFiClient espClient;
PubSubClient mqttClient(espClient);

#define PinMeter1 D0
#define PinMeter2 D1
#define PinRelay1 D2
#define PinRelay2 D3
#define PinButton1 D5
#define PinButton2 D6
#define PinLed1 D7
#define PinLed2 D8

bool relay1state = 0;
bool relay2state = 0;

// To store the "rise ups" from the flow meter pulses
int counterMeter1 = 0;
int counterMeter2 = 0;

//Interrupt function, so that the counting of pulse “rise ups” dont interfere with the rest of the code  (attachInterrupt)
void meter1_triggered() {
  counterMeter1++;
}
void meter2_triggered() {
  counterMeter2++;
}

void tick() {
  //toggle state
  //int state = digitalRead(LED_BUILTIN);  // get the current state of GPIO1 pin
  //digitalWrite(LED_BUILTIN, !state);     // set pin to the opposite state

  Serial.println("tick");
}

void mqttSubscriptionCallback(char* topic, byte* payload, unsigned int length) {
  // report to terminal for debug
  Serial.print("[MQTT] New message arrived in topic: ");
  Serial.println(topic);
  Serial.print("[MQTT] Received message: ");
  for (uint i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
/*
  // check if we received our IN topic for state change
  if (strcmp (TOPIC_IN_FEED, topic) == 0) {
    // toggle output if we need to change state
    if ((char)payload[0] == '1' && !isFeeding) {
      Serial.print ("Toggling state!");
      Servo_OpenClose();
    }
  }
  // check for sync topic
  else if (strcmp (TOPIC_IN_SYNC, topic) == 0) {
    // we should get '1' as sync request
    if ((char) payload[0] == '1') {
      isSyncing = true;
    }
  }
  */
}

void setupMqtt() {  
  if(mqttClient.connected()) {
    Serial.println("[MQTT] Disconnecting...");
    mqttClient.disconnect();
  }

  if(config.mqtt_server.length() == 0) {
    return; // no server, no connection needed
  }
  
  mqttClient.setServer(config.mqtt_server.c_str(), config.mqtt_port);
  mqttClient.setCallback(mqttSubscriptionCallback);

  int retryCount = 0;
  while (!mqttClient.connected() && retryCount < 5) {
      Serial.println("[MQTT] Connecting...");
  
      bool connectionState = false;
      if(config.mqtt_user.length() > 0) {
        connectionState = mqttClient.connect("Zavlazovac", config.mqtt_user.c_str(), config.mqtt_password.c_str());
      } else {
        connectionState = mqttClient.connect("Zavlazovac");
      }
      
      if (connectionState) {
        Serial.println("[MQTT] Connected successfully.");  
      } else {
        Serial.print("[MQTT] Connection failed with state ");
        Serial.println(mqttClient.state());
        delay(2000);
      }

      retryCount++;
  }
}

void toggleRelay(int id) {
  uint8_t relayPin = 0;
  uint8_t ledPin = 0;
  
  switch(id) {
    case 1:
      relayPin = PinRelay1;
      ledPin = PinLed1;
      break;
    case 2:
      relayPin = PinRelay2;
      ledPin = PinLed2;
      break;
    default:
      return;
      break;
  }

  // Do the toggle
  int newValue = !digitalRead(relayPin);
  digitalWrite(relayPin, newValue); // relay
  digitalWrite(ledPin, newValue); // led
}

// Gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}

String SendSettingsHTML(){
  Serial.println("sending html");

  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<title>LED Control</title>\n";
  ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n";
  ptr +=".button {display: block;width: 80px;background-color: #1abc9c;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 4px;}\n";
  ptr +=".button-on {background-color: #1abc9c;}\n";
  ptr +=".button-on:active {background-color: #16a085;}\n";
  ptr +=".button-off {background-color: #34495e;}\n";
  ptr +=".button-off:active {background-color: #2c3e50;}\n";
  ptr +="p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body>\n";
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
  "    <td colspan=\"2\"><input type=\"submit\" value=\"Save Changes\"></td>\n"
  "  </tr>\n"
  "</table>\n";
  
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
  ptr +=".button {display: block;width: 80px;background-color: #1abc9c;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 4px;}\n";
  ptr +=".button-on {background-color: #1abc9c;}\n";
  ptr +=".button-on:active {background-color: #16a085;}\n";
  ptr +=".button-off {background-color: #34495e;}\n";
  ptr +=".button-off:active {background-color: #2c3e50;}\n";
  ptr +="p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body>\n";
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
  ptr +="</body>\n";
  ptr +="</html>\n";
  return ptr;
}

void handle_pageConfig() {
  server.send(200, "text/html", SendSettingsHTML()); 
}

void handle_saveConfig() {
  // Save runtime data
  String arg = server.arg("mqtt_port");
  config.mqtt_port = (arg.length() == 0 ? 1883 : arg.toInt());
  config.mqtt_server = server.arg("mqtt_server");
  config.mqtt_user = server.arg("mqtt_user"); 
  config.mqtt_password = server.arg("mqtt_password");
  config.mqtt_channel = server.arg("mqtt_channel");

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
  setupMqtt();

  server.sendHeader("Location", "/config");
  server.sendHeader("X-Config-Saved", "1");
  server.send(200, "text/html", "OK"); 
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
  server.send(200, "text/html", "OK");
}

void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}

// Callback function to be called when the button is pressed.
void onPressed() {
	Serial.println("Button has been pressed!");
}

void setup() {
  // For testing
  //SPIFFS.format();
  //WiFiManager.reset();

  // Set serial console Baud ate
  Serial.begin(115200);

  // Initialize the button.
	button.begin();
	// Add the callback function to be called when the button is pressed.
	button.onPressed(onPressed);

  // Initialize GPIO PINs
  pinMode(PinLed1, OUTPUT);
  pinMode(PinLed2, OUTPUT);
  pinMode(PinRelay1, OUTPUT);
  pinMode(PinRelay2, OUTPUT);
  pinMode(PinMeter1, INPUT);
  pinMode(PinMeter2, INPUT);
  pinMode(PinButton1, INPUT);
  pinMode(PinButton2, INPUT);
  
  // And attach interrupt watches to meter PINs
  attachInterrupt(digitalPinToInterrupt(PinMeter1), meter1_triggered, RISING);
  attachInterrupt(digitalPinToInterrupt(PinMeter2), meter2_triggered, RISING);

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
  //wifiManager.setAPCallback(configModeCallback);

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
  setupMqtt();

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
  
  // Process MQTT communication
  mqttClient.loop();
  
  // Continuously read the status of the button. 
	button.read();
}
