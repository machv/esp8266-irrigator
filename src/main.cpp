#include <FS.h> // FS needs to be the first
#include <Arduino.h>
#include <WiFiManager.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h> // MQTT server library
#include <EasyButton.h>
#include <ArduinoJson.h>
#include <Ticker.h> // for LED status indications
#include "settings.h" // Application settings
#include "FlowMeter.h" // Flow meter

// HW mapping
#define PinMeter1 D5
#define PinMeter2 D6
#define PinRelay1 D2
#define PinRelay2 D3
#define PinButton1 D7
#define PinButton2 D4
#define PinLed1 D0
#define PinLed2 D1
#define PinLedStatus D8

// Pin arrays
const uint8_t BUTTON_PINS[RELAYS_COUNT] = { PinButton1, PinButton2 };
const uint8_t LED_PINS[RELAYS_COUNT] = { PinLed1, PinLed2 };
const uint8_t RELAY_PINS[RELAYS_COUNT] = { PinRelay1, PinRelay2 };
const uint8_t METER_PINS[RELAYS_COUNT] = { PinMeter1, PinMeter2 };

// schedules LED blinking
Ticker ticker;

// Management web interface
ESP8266WebServer server(80);
#define CSS_FILE "/style.css"
#define JS_FILE "/scripts.js"

// Configuration
const char *ConfigFileName = "/config.json";
Configuration Config; 

// MQTT
WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastMqttConnectionRetryTime;
unsigned long mqttReconnectDelay = 30000;
String mqttLwtTopic;
String mqttTopicRelayStatus[RELAYS_COUNT];
String mqttTopicRelayCommand[RELAYS_COUNT];
bool relayState[RELAYS_COUNT];

EasyButton buttons[RELAYS_COUNT] = { 
  EasyButton(PinButton1), 
  EasyButton(PinButton2) 
};

FlowMeter meters[RELAYS_COUNT] = {
  FlowMeter(PinMeter1),
  FlowMeter(PinMeter2)
};

// The hall-effect flow sensor outputs approximately 4.5 pulses per second per
// litre/minute of flow.
//float calibrationFactor = 4.5;
// https://github.com/sekdiy/FlowMeter/wiki/Properties
// For YF-B5 sensor (f = 6.6 x Q)
float calibrationFactor = 6.6;

// millis when relays should be turned off
unsigned long relayTimeoutWhen[RELAYS_COUNT];

File GetFile(String fileName) {
  File file;
  if (SPIFFS.exists(fileName)) {
    file = SPIFFS.open(fileName, "r");
  }
  return file;
}

// Configuration handling
void readConfigurationFile() {
  Serial.println("[CONFIG] Reading configuration.");

  // set defaults
  for(int i = 0; i < RELAYS_COUNT; i++) {
    Config.relays[i].name = String("Relay " + String(i + 1));
    Config.relays[i].timeout = 0;

    Serial.println(Config.relays[i].name);
  }

  // read config file
  if(SPIFFS.exists(ConfigFileName))
  {
    Serial.println("Reading existing configuration.");

    File configFile = SPIFFS.open(ConfigFileName, "r");

    StaticJsonDocument<512> json;
    DeserializationError error = deserializeJson(json, configFile);
    if (error) {
      Serial.println(F("Failed to read file, using default configuration"));
      return;
    }

    // Copy values from the JsonDocument to the Config
    // https://arduinojson.org/v6/example/config/
    Config.mqtt_port = json["mqtt_port"] | 1883;
    Config.mqtt_server = json["mqtt_server"].as<String>();
    Config.mqtt_user = json["mqtt_user"].as<String>();
    Config.mqtt_password = json["mqtt_password"].as<String>();
    Config.mqtt_channel_prefix = json["mqtt_channel_prefix"].as<String>();
    
    if(json.containsKey("relays")) {
      JsonArray relays = json["relays"].as<JsonArray>();
      if(!relays.isNull()) {
        int i = 0;
        for(JsonObject relay : relays) {
          Config.relays[i].name = relay["name"].as<String>();
          Config.relays[i].timeout = json["timeout"] | 0;
          i++;
        }
      }
    }

    configFile.close();
  }
}

void saveConfigurationFile() {
  Serial.println("Saving configuration file.");

  File configFile = SPIFFS.open(ConfigFileName, "w");

  // Use arduinojson.org/assistant to compute the capacity.
  StaticJsonDocument<640> jsonDocument;

  // Set global values 
  jsonDocument["mqtt_server"] = Config.mqtt_server;
  jsonDocument["mqtt_port"] = Config.mqtt_port;
  jsonDocument["mqtt_user"] = Config.mqtt_user;
  jsonDocument["mqtt_password"] = Config.mqtt_password;
  jsonDocument["mqtt_channel_prefix"] = Config.mqtt_channel_prefix;

  // and per relay
  JsonArray relays = jsonDocument.createNestedArray("relays");
  for(int i = 0; i < RELAYS_COUNT; i++) {
    JsonObject relay = relays.createNestedObject();
    relay["name"] = Config.relays[i].name;
    relay["timeout"] = Config.relays[i].timeout;
  }

  // Serialize JSON to file
  if (serializeJson(jsonDocument, configFile) == 0) {
    Serial.println(F("Failed to write settings file."));
  }

  // Close the file
  configFile.close();
}

void tickStatusLed() {
  //toggle state
  int state = digitalRead(PinLedStatus);  // get the current state of GPIO1 pin
  digitalWrite(PinLedStatus, !state);     // set pin to the opposite state

  Serial.println("Status LED tick.");
}

void toggleRelay(int id) {
  if(id >= RELAYS_COUNT) {
    Serial.printf("[RELAY] Wrong relay ID (%i) passed, ignoring.", id);
    Serial.println();
    return;
  }
  
  uint8_t relayPin = RELAY_PINS[id];
  uint8_t ledPin = LED_PINS[id];
  String channel = mqttTopicRelayStatus[id];

  int currentValue = digitalRead(relayPin);
  int newValue = !currentValue;

  Serial.printf("Toggling relay #%i from %i to %i.", id, currentValue, newValue);
  Serial.println();

  // Do the toggle
  // HIGH (0x1) = OFF, LOW (0x0) = ON
  digitalWrite(relayPin, newValue); // relay
  digitalWrite(ledPin, !newValue); // led
  
  relayState[id] = !newValue;

  if(relayState[id] && Config.relays[id].timeout > 0) { // if enabling and is timeout set, activate
    relayTimeoutWhen[id] = millis() + (Config.relays[id].timeout * 60 * 1000);
  } else {
    relayTimeoutWhen[id] = 0;
  }

  // And publish state update via MQTT
  if(mqttClient.connected()) {
    Serial.println("[MQTT] Publishing updated state after toggle.");

    String value = String(relayState[id]);
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

  for(int i = 0; i < RELAYS_COUNT; i++) {
    if (strcmp (mqttTopicRelayCommand[i].c_str(), topic) == 0 && length > 0) {
      Serial.printf("[MQTT] State of relay %i requested to %c", (i + 1), payload[0]);

      if(
        ((payload[0] == '1' || (strcmp((char *)payload, "ON")  == 0) ) && relayState[i] == false) ||
        ((payload[0] == '0' || (strcmp((char *)payload, "OFF") == 0) ) && relayState[i] == true)
      ) {
        Serial.printf(", current state %i differs -> toggle.", relayState[i]);
       
        toggleRelay(i);
      } else {
        Serial.print(" already current state.");
      }

      Serial.println();
      return;
    }
  }

  // If we get here, we've not matched anything in loop above
  Serial.println("[MQTT] No match for any action.");
}

void setupMqtt(int retries) { 
  if(mqttClient.connected()) {
    Serial.println("[MQTT] Disconnecting...");

    // publish offline status to LWT (as when gracefully Disconnecting no LWT is sent)
    mqttClient.publish(mqttLwtTopic.c_str(), "Offline", true);

    // and gracefully disconnect
    mqttClient.disconnect();
  }

  if(Config.mqtt_server.length() == 0) {
    return; // no server, no connection needed
  }
  
  mqttLwtTopic = String(Config.mqtt_channel_prefix + "status");
  for(int i = 0; i < RELAYS_COUNT; i++) {
    // in MQTT relays are numbered starting with 1, not 0
    mqttTopicRelayStatus[i] = String(Config.mqtt_channel_prefix + "state/" + (i + 1));
    mqttTopicRelayCommand[i] = String(Config.mqtt_channel_prefix + "command/" + (i + 1)) + "/power";
  }

  mqttClient.setServer(Config.mqtt_server.c_str(), Config.mqtt_port);
  mqttClient.setCallback(mqttSubscriptionCallback);

  const char *mqtt_user = nullptr;
  const char *mqtt_password = nullptr;
  if (Config.mqtt_user.length() > 0) 
    mqtt_user = Config.mqtt_user.c_str();
  if (Config.mqtt_user.length() > 0) 
    mqtt_password = Config.mqtt_password.c_str();

  // Build Client ID from MAC Address
  uint32_t chipId = ESP.getChipId();
  char clientId[20];
  snprintf(clientId, 20, "Zavlazovac-%08X", chipId);

  int retryCount = 0;
  while (!mqttClient.connected() && retryCount < retries) {
      Serial.printf("[MQTT] Connecting (retry %d of %d) with identity %s...", (retryCount + 1), retries, clientId);
      Serial.println();
  
      bool connectionState = false;
      connectionState = mqttClient.connect(clientId, mqtt_user, mqtt_password, mqttLwtTopic.c_str(), 1, true, "Offline");
      
      if (connectionState) {
        Serial.println("[MQTT] Connected successfully.");  

        // publish online status to LWT
        mqttClient.publish(mqttLwtTopic.c_str(), "Online", true);
      } else {
        Serial.print("[MQTT] Connection failed with code: ");
        Serial.println(mqttClient.state());

        if(retryCount > 0) { // delay only if more retries are requested
          delay(1000);
        }
      }

      retryCount++;
  }

  // And subscribe to respective channels
  if(mqttClient.connected()) {
    for(int i = 0; i < RELAYS_COUNT; i++) {
      Serial.printf("[MQTT] Publishing current state of relay %d.\n", i);
      String value = String(relayState[i]);
      mqttClient.publish(mqttTopicRelayStatus[i].c_str(), value.c_str());

      Serial.printf("[MQTT] Subscribing to the command channel: %s\n", mqttTopicRelayCommand[i].c_str());
      mqttClient.subscribe(mqttTopicRelayCommand[i].c_str());
    }
  }

  lastMqttConnectionRetryTime = millis();
}

void reconnectMqtt() {
  setupMqtt(5); // By default try 5-times before gave up
}

// Gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tickStatusLed);
}

char * millisToString(unsigned long millis) {
  unsigned long seconds = millis / 1000;
  static char str[12];
  long h = seconds / 3600;
  seconds = seconds % 3600;
  int m = seconds / 60;
  int s = seconds % 60;
  sprintf(str, "%02ld:%02d:%02d", h, m, s);
 
  return str;
}

void handle_cssFile() {
  File cssFile = GetFile(CSS_FILE);
  server.streamFile(cssFile, "text/css");
  cssFile.close();
}

void handle_jsFile() {
  File jsFile = GetFile(JS_FILE);
  server.streamFile(jsFile, "text/css");
  jsFile.close();
}

void handle_notFound() {
  server.send(404, "text/plain", "Not found");
}

String generateSettingsHtml(){
  Serial.println("[HTTP] Sending /config page.");

  String ptr = "<!DOCTYPE html> <html>\n";
  ptr += "<head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr += "<title>Zavlažovač / Settings</title>\n";
  ptr += "<link href=\"/style.css\" type=\"text/css\" rel=\"stylesheet\"/>\n";
  ptr += "</head>\n";
  ptr += "<body><div class=\"page\">\n";
  ptr += "<h1>Zavlažovač</h1>\n";
  ptr += "<h2>Settings</h2>\n";
  
  if(server.hasArg("saved")) {
    ptr += "<div class=\"alert-box\">Configuration was saved.</div>";
  }

  ptr +="<form method=\"post\" enctype=\"application/x-www-form-urlencoded\">\n";
  ptr +="<table>\n";

  for(int i = 0; i < RELAYS_COUNT; i++) {
    String id = String(i);

    ptr += ""
      "   <tr>\n"
      "     <th colspan=\"2\" class=\"settings-cell\">Relay " + String(i + 1) + "</th>\n"
      "   </tr>\n"
      "  <tr>\n"
      "    <th>Name</th>\n"
      "    <td><input type=\"text\" name=\"relay_"+ id + "_name\" value=\"" + (Config.relays[i].name) + "\"></td>\n"
      "  </tr>\n"
      "  <tr>\n"
      "    <th>Timeout</th>\n"
      "    <td><input type=\"text\" name=\"relay_" + id + "_timeout\" value=\"" + (Config.relays[i].timeout) + "\"> min.<div class=\"small\">In minutes, 0 means no timeout.</div></td>\n"
      "  </tr>\n";
  }

   ptr += ""
    "<tr>"
    "<th colspan=\"2\" class=\"settings-cell\">MQTT Settings</th>"
    "</tr>"
    "  <tr>\n"
    "    <th>Server</th>\n"
    "    <td><input type=\"text\" name=\"mqtt_server\" value=\"" + (Config.mqtt_server) + "\"></td>\n"
    "  </tr>\n"

    "  <tr>\n"
    "    <th>Port</th>\n"
    "    <td><input type=\"text\" name=\"mqtt_port\" value=\"" + (Config.mqtt_port) + "\"></td>\n"
    "  </tr>\n"

    "  <tr>\n"
    "    <th>User</th>\n"
    "    <td><input type=\"text\" name=\"mqtt_user\" value=\"" + (Config.mqtt_user) + "\"></td>\n"
    "  </tr>\n"

      "  <tr>\n"
    "    <th>Password</th>\n"
    "    <td><input type=\"password\" name=\"mqtt_password\" value=\"" + (Config.mqtt_password) + "\"></td>\n"
    "  </tr>\n"

    "  <tr>\n"
    "    <th>Channel prefix</th>\n"
    "    <td><input type=\"text\" name=\"mqtt_channel_prefix\" value=\"" + (Config.mqtt_channel_prefix) + "\"></td>\n"
    "  </tr>\n"

    "  <tr>\n"
    "    <th class=\"settings-cell\" colspan=\"2\"><input class=\"button\" type=\"submit\" value=\"Save Changes\"></th>\n"
    "  </tr>\n"

    "  <tr>\n"
    "    <th class=\"settings-cell\" colspan=\"2\"><a href=\"/\" class=\"button\">Back</a></th>\n"
    "  </tr>\n"
    "</table>\n"
    "</form>\n"
    "</body>\n"
    "</html>\n";
  
  return ptr;
}

String generateJsonApiResponse() {
   StaticJsonDocument<640> jsonDocument;
  
  JsonArray relays = jsonDocument.createNestedArray("relays");
  for(int i = 0; i < RELAYS_COUNT; i++) {
    JsonObject relay = relays.createNestedObject();
    relay["timeout"] = 0;
    if(Config.relays[i].timeout > 0 && relayTimeoutWhen[i] > 0) {
      relay["timeout"] = (relayTimeoutWhen[i] - millis()) / 1000;
    }
    relay["state"] = relayState[i];
    relay["flowMilliLitres"] = meters[i].flowMilliLitres / 1000.0;
    relay["totalMilliLitres"] = meters[i].totalMilliLitres / 1000.0;
    relay["flowRate"] = meters[i].flowRate;
  }

  String json;
  serializeJson(jsonDocument, json);

  return json;
}

void handle_api() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  server.send(200, "application/json", generateJsonApiResponse()); 
}

String generateHomepageHtml(){
  Serial.println("[HTTP] Sending homepage.");

  String ptr = "<!DOCTYPE html> <html>\n";
  ptr += "<head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr += "<title>Irrigation</title>\n";
  ptr += "<link href=\"/style.css\" type=\"text/css\" rel=\"stylesheet\"/>\n";
  ptr += "<script type=\"text/javascript\" src=\"/scripts.js\"></script>\n";
  
  ptr += "<script type=\"text/javascript\">\n"
         "  window.onload = function () {\n";
  for(int i = 0; i < RELAYS_COUNT; i++) {
    if(Config.relays[i].timeout > 0 && relayTimeoutWhen[i] > 0) {
      unsigned long remainingSecondsTimer = (relayTimeoutWhen[i] - millis()) / 1000;
      ptr += "    updateRelayCountdown(" + String(i) + ", " + String(remainingSecondsTimer) + ");\n";
    }
  } 
  ptr += ""
         "    // start updating of the current UI values\n"
         "    refresher(10000); // in ms\n"
         "    // start timers\n"
         "    startCountdowns();\n"
         "  };\n";
  ptr += "</script>\n";
  ptr += "</head>\n";
  ptr += "<body>\n<div class=\"page\">";
  ptr += "<h1>Irrigation</h1>\n";
  
  unsigned int frac;

  for(int i = 0; i< RELAYS_COUNT; i++) {
    ptr += "<div>";
    ptr += "<h2>" + Config.relays[i].name + "</h2>";

    ptr += "<h3 id=\"r" + String(i) + "_timerCountdown\"></h3>";

    ptr += "<table>";
    ptr += "<tr>"
          "<td colspan=\"2\" id=\"r" + String(i) + "_state\" class=\"relay-state\">";

    if(relayState[i] == true) {
      ptr += "ON";
    } else {
      ptr += "OFF";
    }
    ptr += "</td></tr>";

    ptr += "<tr>"
          "<td colspan=\"2\" class=\"settings-cell\">";

    if(relayState[i] == true) {
      ptr += "<a id=\"r" + String(i) + "_btn\" class=\"button button-on\" href=\"/toggle?id=" + String(i) + "\">Turn OFF</a>";
    } else {
      ptr += "<a id=\"r" + String(i) + "_btn\" class=\"button button-off\" href=\"/toggle?id=" + String(i) + "\">Turn ON</a>";
    }
    ptr += "</td></tr>";
/*
    if(Config.relays[i].timeout > 0 && relayTimeoutWhen[i] > 0) {
      ptr += "<tr>"
          "<th>Timer off after:</th>"
          "<td id=\"r" + String(i) + "_timerWhen\">";
      ptr += millisToString(relayTimeoutWhen[i] - millis());
      ptr += "</td></tr>";
    }
*/
      ptr += "<tr>"
          "<th>Flow rate:</th>"
          "<td><span id=\"r" + String(i) + "_flowRate\">";
    ptr += int(meters[i].flowRate); // Print the integer part of the variable
    ptr += "."; // Print the decimal point
    // Determine the fractional part. The 10 multiplier gives us 1 decimal place.
    frac = (meters[i].flowRate - int(meters[i].flowRate)) * 10;
    ptr += frac; // Print the fractional part of the variable
    ptr += "</span> L/min</td>"
           "</tr>"
           "<tr>"
           "<th>Current flow:</th>"
           "<td><span id=\"r" + String(i) + "_flowMilliLitres\">" + String(meters[i].flowMilliLitres / 1000.0) + "</span> L/sec</td>"
           "</tr>"
           "<tr>"
           "<th>Total Quantity:</th>"
           "<td><span id=\"r" + String(i) + "_totalMilliLitres\">" + String(meters[i].totalMilliLitres / 1000.0) + "</span> L</td>"
           "</tr>";
    ptr += "</table>";
    ptr += "</div>";
  }

  //"<table>"
         //"<tr>"
         //"<td colspan=\"2\" class=\"settings-cell\">"
  ptr += "<a style='display: inline-block' class=\"button button-danger\" href=\"/config\">Configuration</a>\n";
         //"</td>"
         //"</tr>";

  ptr += "<tr>"
         "<td colspan=\"2\" class=\"settings-cell\">"
         "<p><form action='/restart' method='get' onsubmit='return confirm(\"Do you really want to restart the device?\");'>"
         "<button name='restart' class='button button-danger'>Restart</button></form></p>"
         "</tr>";

  ptr += "</table>";

  ptr +="</div></body>\n";
  ptr +="</html>\n";
  return ptr;
}

void handle_pageConfig() {
  server.send(200, "text/html", generateSettingsHtml()); 
}

void handle_saveConfig() {
  String arg;
  arg = server.arg("mqtt_port");
  arg.trim();
  
  Config.mqtt_port = (arg.length() == 0 ? 1883 : arg.toInt());
  
  arg = server.arg("mqtt_server");
  arg.trim();
  Config.mqtt_server = arg;

  arg = server.arg("mqtt_user");
  arg.trim();
  Config.mqtt_user = arg; 

  arg = server.arg("mqtt_password");
  arg.trim();
  Config.mqtt_password = arg;

  String channel = server.arg("mqtt_channel_prefix");
  channel.trim();
  if(!channel.endsWith("/"))
    channel += "/";
  Config.mqtt_channel_prefix = channel;

  for(int i = 0; i < RELAYS_COUNT; i++) {
    arg = server.arg("relay_" + String(i) + "_timeout");
    arg.trim();
    Config.relays[i].timeout = (arg.length() == 0 ? 0 : arg.toInt());

    arg = server.arg("relay_" + String(i) + "_name");
    arg.trim();
    Config.relays[i].name = arg;
  }

  saveConfigurationFile();

  // Reconnect MQTT to reflect changes
  reconnectMqtt();

  server.sendHeader("Location", "/config?saved=1", true);
  server.send(303, "text/plain"); 
}

void handle_homepage() {
  server.send(200, "text/html", generateHomepageHtml()); 
}

void handle_restart() {
  server.send(200, "text/html", "<strong>Restarting the device...</strong>"); 
  delay(200);
  ESP.restart();
}

void handle_toggle() {
  if(!server.hasArg("id")) {
    server.send(400, "text/html", "Missing required parameter ID.");
    return;
  }

  int id = server.arg("id").toInt();
  if(id >= RELAYS_COUNT) {
    server.send(400, "text/html", "Invalid ID of relay was sent.");
    return;
  }

  toggleRelay(id);

  server.sendHeader("Location", "/");
  server.send(303, "text/plain");
}

// Callback function to be called when the button is pressed.
void onPressed0() {
	Serial.println("Button 0 has been pressed.");
  
  toggleRelay(0);
}

void onPressed1() {
	Serial.println("Button 1 has been pressed.");
  
  toggleRelay(1);
}

void ICACHE_RAM_ATTR meter0_triggered() {
  meters[0].counter();

  //Serial.printf("[FLOW] Flow meter 1 triggered, current counter = %d", meters[0]._pulseCounter);
  //Serial.println();
}
void ICACHE_RAM_ATTR meter1_triggered() {
  meters[1].counter();

  //Serial.printf("[FLOW] Flow meter 2 triggered, current counter = %d", meters[1]._pulseCounter);
  //Serial.println();
}

void meter_flowChanged(uint8_t pin) {
  int meterIndex = 0;
  bool meterFound = false;
  for(meterIndex = 0; meterIndex < RELAYS_COUNT; meterIndex++) {
    if(METER_PINS[meterIndex] == pin) {
      meterFound = true;
      break;
    }
  }

  if(!meterFound) {
    Serial.printf("[FLOW] Unable to find meter index for PIN %d", pin);
    Serial.println();

    return;
  }

  if(meters[meterIndex].flowRate > 0) {
    // Print the flow rate for this second in litres / minute
    Serial.printf("[Valve %i] Flow rate: %.2f L/min", meterIndex, meters[meterIndex].flowRate);

    // Print the number of litres flowed in this second
    Serial.printf("  Current Liquid Flowing: %d mL/sec", meters[meterIndex].flowMilliLitres); // Output separator

    // Print the cumulative total of litres flowed since starting
    Serial.printf("  Output Liquid Quantity: %lu mL", meters[meterIndex].totalMilliLitres); // Output separator
    Serial.println();
  }

  if(mqttClient.connected()) {
    Serial.printf("[Valve %i] Reporting to MQTT", meterIndex);
    Serial.println();

    String channelCurrent = String(Config.mqtt_channel_prefix + meterIndex + "/currentFlow");
    String valueCurrent = String(meters[meterIndex].flowRate);
    mqttClient.publish(channelCurrent.c_str(), valueCurrent.c_str());

    String channelTotal = String(Config.mqtt_channel_prefix + meterIndex + "/totalFlow");
    String valueTotal = String(meters[meterIndex].totalMilliLitres);
    mqttClient.publish(channelTotal.c_str(), valueTotal.c_str());
  }
}

void setup() {
  // For testing
  //SPIFFS.format();
  //WiFiManager.reset();

  // Set serial console Baud rate
  Serial.begin(115200);

  // Initialize the buttons
	buttons[0].begin();
  buttons[0].onPressed(onPressed0);
  
  buttons[1].begin();
  buttons[1].onPressed(onPressed1);
	
  // Initialize GPIO PINs
  pinMode(PinLed1, OUTPUT);
  pinMode(PinLed2, OUTPUT);
  pinMode(PinRelay1, OUTPUT);
  digitalWrite(PinRelay1, HIGH); // by default turn it off (=HIGH)
  pinMode(PinRelay2, OUTPUT);
  digitalWrite(PinRelay2, HIGH); // by default turn it off (=HIGH)

  // Prepare meters...
  meters[0].begin(meter0_triggered);
  meters[1].begin(meter1_triggered);
  for(int i = 0; i < RELAYS_COUNT; i++) {
    meters[i].onFlowChanged(meter_flowChanged);
  }

  // !!! using internal LED (LED_BUILTIN) blocks internal TTY output !!! On-board LED je připojena mezi TX1 = GPIO2 a VCC 

  // Set led pin as output
  pinMode(PinLedStatus, OUTPUT);
  // Start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tickStatusLed);

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

  // If we get here we have connected to the WiFi
  ticker.detach();
  // Keep status LED on
  digitalWrite(PinLedStatus, HIGH);

  // Init values from file system
  if (SPIFFS.begin()) {
    Serial.println("File system is mounted.");

    readConfigurationFile();
  }

  // Reconnect MQTT if needed
  reconnectMqtt();

  // Web server pages
  server.on("/", handle_homepage);
  server.on("/config", HTTP_GET, handle_pageConfig);
  server.on("/config", HTTP_POST, handle_saveConfig);
  server.on("/api/current", handle_api);
  server.on("/restart", handle_restart);
  server.on("/toggle", handle_toggle);
  server.on("/style.css", handle_cssFile);
  server.on("/scripts.js", handle_jsFile);
  server.onNotFound(handle_notFound);  
  server.begin();
  Serial.println("[HTTP] Server started.");
}

void loop() {
  // Process web server requests
  server.handleClient();
  
  // MQTT reconnect if no connection with non-blocking delay
  if(!mqttClient.connected() && ((millis() - lastMqttConnectionRetryTime) > mqttReconnectDelay)) {
    setupMqtt(1);
  }
  
  // Process MQTT communication
  mqttClient.loop();
  
  for(int i = 0; i < RELAYS_COUNT; i++) {
    // Continuously read the status of the button. 
    buttons[i].read();

    // process flow meters
    meters[i].loop();

    // Process relay timeouts
    if(Config.relays[i].timeout > 0 && relayTimeoutWhen[i] > 0 && relayTimeoutWhen[i] < millis()) {
      Serial.println("Configured timeout for relay 1 exceeded -> toggling");
      toggleRelay(i);
    }
  }
}
