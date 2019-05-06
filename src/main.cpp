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
  String relay_1_name;
  int relay_1_timeout;
  String relay_2_name;
  int relay_2_timeout;
};
const char *ConfigFileName = "/config.json";
Config config; 

unsigned long relay1timeoutWhen;
unsigned long relay2timeoutWhen;

ESP8266WebServer server(80);

// MQTT
WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastMqttConnectionRetryTime;
unsigned long mqttReconnectDelay = 30000;
String mqttTopicRelay1;
String mqttTopicRelay2;

#define PinMeter1 D5
#define PinMeter2 D6
#define PinRelay1 D2
#define PinRelay2 D3
#define PinButton1 D7
#define PinButton2 D4
#define PinLed1 D0
#define PinLed2 D1
#define PinLedStatus D8

bool relay1state = false;
bool relay2state = false;

EasyButton button1(PinButton1);
EasyButton button2(PinButton2);

// The hall-effect flow sensor outputs approximately 4.5 pulses per second per
// litre/minute of flow.
float calibrationFactor = 4.5;

// To store the "rise ups" from the flow meter pulses
volatile uint pulseCounterMeter1 = 0;
volatile uint pulseCounterMeter2 = 0;

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

void tickStatusLed() {
  //toggle state
  int state = digitalRead(PinLedStatus);  // get the current state of GPIO1 pin
  digitalWrite(PinLedStatus, !state);     // set pin to the opposite state

  Serial.println("Status LED tick.");
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

    if(relay1state && config.relay_1_timeout > 0) { // if enabling and is timeout set, activate
      relay1timeoutWhen = millis() + (config.relay_1_timeout * 60 * 1000); // timeout in settings is in minutes
    } else {
      relay1timeoutWhen = 0;
    }

  } else if (id == 2) {
    relay2state = !newValue;

    if(relay2state && config.relay_2_timeout > 0) { // if enabling and is timeout set, activate
      relay2timeoutWhen = millis() + (config.relay_2_timeout * 60 * 1000); // timeout in settings is in minutes
    } else {
      relay2timeoutWhen = 0;
    }
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
  ticker.attach(0.2, tickStatusLed);
}

String SendStylesheetContent() {
  String ptr;

  ptr += "html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr += "body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 5px;}\n";
  ptr += ".button {display: block;background-color: #1abc9c;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 15px;cursor: pointer;border-radius: 4px;}\n";
  ptr += ".button-on {background-color: #1abc9c;}\n";
  ptr += ".button-on:active {background-color: #16a085;}\n";
  ptr += ".button-off {background-color: #34495e;}\n";
  ptr += ".button-off:active {background-color: #2c3e50;}\n";
  ptr += ".settings-cell {text-align: center; font-size: 14pt; padding: 4px; padding-top: 20px}\n";
  ptr += "th, td {text-align: left; font-size: 12pt}";
  ptr += "input { padding: 5px; font-size: 12pt}";
  ptr += "table {margin: 0 auto; margin-bottom: 20px}\n";
  ptr += ".small { font-size: 75%}\n";
  ptr += "p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";

  return ptr;
}

void handle_stylesheetFile() {
  server.send(200, "text/css", SendStylesheetContent()); 
}

String SendJavascriptContent() {
  String str = "";

  str += "function startTimer(duration, display) {\n"
    "var timer = duration, minutes, seconds;\n"
    "setInterval(function () {\n"
    "    minutes = parseInt(timer / 60, 10)\n"
    "    seconds = parseInt(timer % 60, 10);\n"
    "\n"
    "    minutes = minutes < 10 ? \"0\" + minutes : minutes;\n"
    "    seconds = seconds < 10 ? \"0\" + seconds : seconds;\n"
    "\n"
    "    display.textContent = minutes + \":\" + seconds;\n"
    "\n"
    "    if (--timer < 0) {\n"
    "        display.textContent = \"\";\n"         
    "    //    timer = duration;\n"
    "    }\n"
    "}, 1000);\n"
    "}\n";

  return str;
}

void handle_jsFile() {
  server.send(200, "text/javascript", SendJavascriptContent());
}

String SendSettingsHTML(){
  Serial.println("Sending Config HTML.");

  String ptr = "<!DOCTYPE html> <html>\n";
  ptr += "<head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr += "<title>Zavlažovač / Settings</title>\n";
  ptr += "<link href=\"/style.css\" type=\"text/css\" rel=\"stylesheet\"/>\n";
  ptr += "</head>\n";
  ptr += "<body><div class=\"page\">\n";
  ptr += "<h1>Zavlažovač</h1>\n";
  ptr += "<h2>Settings</h2>\n";
  
  if(server.hasArg("saved")) {
    ptr += "<div style=\"text-align: center; margin: 20px; font-weight: bold\">Configuration was saved.</div>";
  }

  ptr +="<form method=\"post\" enctype=\"application/x-www-form-urlencoded\">\n";
  ptr +="<table>\n"  
    "<tr>"
    " <th colspan=\"2\" class=\"settings-cell\">Relay 1</th>"
    "</tr>"
    
    "  <tr>\n"
    "    <th>Name</th>\n"
    "    <td><input type=\"text\" name=\"relay_1_name\" value=\"" + (config.relay_1_name) + "\"></td>\n"
    "  </tr>\n"
    
    "  <tr>\n"
    "    <th>Timeout</th>\n"
    "    <td><input type=\"text\" name=\"relay_1_timeout\" value=\"" + (config.relay_1_timeout) + "\"> min.<div class=\"small\">In minutes, 0 means no timeout.</div></td>\n"
    "  </tr>\n"

    "<tr>"
    " <th colspan=\"2\" class=\"settings-cell\">Relay 2</th>"
    "</tr>"

    "  <tr>\n"
    "    <th>Name</th>\n"
    "    <td><input type=\"text\" name=\"relay_2_name\" value=\"" + (config.relay_2_name) + "\"></td>\n"
    "  </tr>\n"

    "  <tr>\n"
    "    <th>Timeout</th>\n"
    "    <td><input type=\"text\" name=\"relay_2_timeout\" value=\"" + (config.relay_2_timeout) + "\"> min.<div class=\"small\">In minutes, 0 means no timeout.</div></td>\n"
    "  </tr>\n"

  "<tr>"
  "<th colspan=\"2\" class=\"settings-cell\">MQTT Settings</th>"
  "</tr>"
  "  <tr>\n"
  "    <th>Server</th>\n"
  "    <td><input type=\"text\" name=\"mqtt_server\" value=\"" + (config.mqtt_server) + "\"></td>\n"
  "  </tr>\n"

  "  <tr>\n"
  "    <th>Port</th>\n"
  "    <td><input type=\"text\" name=\"mqtt_port\" value=\"" + (config.mqtt_port) + "\"></td>\n"
  "  </tr>\n"

  "  <tr>\n"
  "    <th>User</th>\n"
  "    <td><input type=\"text\" name=\"mqtt_user\" value=\"" + (config.mqtt_user) + "\"></td>\n"
  "  </tr>\n"

    "  <tr>\n"
  "    <th>Password</th>\n"
  "    <td><input type=\"password\" name=\"mqtt_password\" value=\"" + (config.mqtt_password) + "\"></td>\n"
  "  </tr>\n"

  "  <tr>\n"
  "    <th>Channel prefix</th>\n"
  "    <td><input type=\"text\" name=\"mqtt_channel\" value=\"" + (config.mqtt_channel) + "\"></td>\n"
  "  </tr>\n"

  "  <tr>\n"
  "    <th class=\"settings-cell\" colspan=\"2\"><input class=\"button\" type=\"submit\" value=\"Save Changes\"></th>\n"
  "  </tr>\n"

  "  <tr>\n"
  "    <th class=\"settings-cell\" colspan=\"2\"><a href=\"/\" class=\"button\">Back</a></th>\n"
  "  </tr>\n"
  "</table>\n";
  
  ptr +="</form>\n"
        "</body>\n"
        "</html>\n";
  
  return ptr;
}

// t is time in seconds = millis()/1000;
char * millisToString(unsigned long millis)
{
  unsigned long seconds = millis / 1000;
  static char str[12];
  long h = seconds / 3600;
  seconds = seconds % 3600;
  int m = seconds / 60;
  int s = seconds % 60;
 sprintf(str, "%02ld:%02d:%02d", h, m, s);
 
 return str;
}

String SendHTML(){
  Serial.println("Sending homepage HTML.");

  String ptr = "<!DOCTYPE html> <html>\n";
  ptr += "<head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr += "<title>Zavlažovač</title>\n";
  ptr += "<link href=\"/style.css\" type=\"text/css\" rel=\"stylesheet\"/>\n";
  ptr += "<script type=\"text/javascript\" src=\"scripts.js\"></script>\n";
  
  if(config.relay_1_timeout > 0 && relay1timeoutWhen > 0) {
    unsigned long remainingSecondsTimer1 = (relay1timeoutWhen - millis()) / 1000;
    ptr += "<script type=\"text/javascript\">\n"
          "window.onload = function () {\n"
          "  var timerSeconds = ";
    ptr += remainingSecondsTimer1;
    ptr += ";\n"
          "  var display = document.querySelector('#relay_1_timer');\n"
          "   startTimer(timerSeconds, display);\n"
          "};\n"
          "</script>";
  }

  if(config.relay_2_timeout > 0 && relay2timeoutWhen > 0) {
    unsigned long remainingSecondsTimer2 = (relay2timeoutWhen - millis()) / 1000;
    ptr += "<script type=\"text/javascript\">\n"
          "window.onload = function () {\n"
          "  var timerSeconds = ";
    ptr += remainingSecondsTimer2;
    ptr += ";\n"
          "  var display = document.querySelector('#relay_2_timer');\n"
          "   startTimer(timerSeconds, display);\n"
          "};\n"
          "</script>";
  }

  ptr += "</head>\n";
  ptr += "<body>\n<div class=\"page\">";
  ptr += "<h1>Zavlažovač</h1>\n";
  
  unsigned int frac;

  ptr += "<div>";
  ptr += "<h2>" + config.relay_1_name + "</h2>";

  if(config.relay_1_timeout > 0 && relay1timeoutWhen > 0) {
    ptr += "<h3 id=\"relay_1_timer\"></h3>";
  }

  ptr += "<table>";
  ptr += "<tr>"
         "<td colspan=\"2\" class=\"settings-cell\">";

  if(relay1state == true) {
    ptr += "<a class=\"button button-on\" href=\"/toggle?id=1\">Turn OFF</a>";
  } else {
    ptr += "<a class=\"button button-off\" href=\"/toggle?id=1\">Turn ON</a>";
  }
  ptr += "</td></tr>";

  if(config.relay_1_timeout > 0 && relay1timeoutWhen > 0) {
    ptr += "<tr>"
        "<th>Timer off after:</th>"
        "<td>";
    ptr += millisToString(relay1timeoutWhen - millis());
    ptr += "</td></tr>";
  }

    ptr += "<tr>"
        "<th>Flow rate:</th>"
        "<td>";
  ptr += int(flowRate1); // Print the integer part of the variable
  ptr += "."; // Print the decimal point
  // Determine the fractional part. The 10 multiplier gives us 1 decimal place.
  frac = (flowRate1 - int(flowRate1)) * 10;
  ptr += frac; // Print the fractional part of the variable
  ptr += " L/min</td>"
        "</tr>"
        "<tr>"
        "<th>Current Liquid Flowing:</th>"
        "<td>";
  ptr += flowMilliLitres1;
  ptr += " mL/Sec</td>"
        "</tr>"
        "<tr>"
        "<th>Output Liquid Quantity:</th>"
        "<td>";
  ptr += totalMilliLitres1;
  ptr += " mL</td>"
         "</tr>";
  ptr += "</table>";
  ptr += "</div>";

  ptr += "<div>";
  ptr += "<h2>" + config.relay_2_name + "</h2>";

  if(config.relay_2_timeout > 0 && relay2timeoutWhen > 0) {
    ptr += "<h3 id=\"relay_2_timer\"></h3>";
  }

  ptr += "<table>";
  ptr += "<tr>"
         "<td colspan=\"2\" class=\"settings-cell\">";
  
  if(relay2state == true) {
    ptr += "<a class=\"button button-on\" href=\"/toggle?id=2\">Turn OFF</a>";
  } else {
    ptr += "<a class=\"button button-off\" href=\"/toggle?id=2\">Turn ON</a>";
  }
  ptr += "</td></tr>";

  if(config.relay_2_timeout > 0 && relay2timeoutWhen > 0) {
    ptr += "<tr>"
        "<th>Turn off after:</th>"
        "<td>";
    ptr += millisToString(relay2timeoutWhen - millis());
    ptr += "</td></tr>";
  }

  ptr += "<tr>"
         "<th>Flow rate:</th>"
         "<td>";
  ptr += int(flowRate2); // Print the integer part of the variable
  ptr += "."; // Print the decimal point
  // Determine the fractional part. The 10 multiplier gives us 1 decimal place.
  frac = (flowRate2 - int(flowRate2)) * 10;
  ptr += frac; // Print the fractional part of the variable
  ptr += " L/min</td>"
        "</tr>"
        "<tr>"
        "<th>Current Liquid Flowing:</th>"
        "<td>";
  ptr += flowMilliLitres2;
  ptr += " mL/Sec</td>"
        "</tr>"
        "<tr>"
        "<th>Output Liquid Quantity:</th>"
        "<td>";
  ptr += totalMilliLitres2;
  ptr += " mL</td>"
         "</tr>";
  ptr += "</table>";

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
  String arg;
  arg = server.arg("mqtt_port");
  arg.trim();
  
  config.mqtt_port = (arg.length() == 0 ? 1883 : arg.toInt());
  config.mqtt_server = server.arg("mqtt_server");
  config.mqtt_user = server.arg("mqtt_user"); 
  config.mqtt_password = server.arg("mqtt_password");
  config.relay_1_name = server.arg("relay_1_name");
  config.relay_2_name = server.arg("relay_2_name");
  
  arg = server.arg("relay_1_timeout");
  arg.trim();
  config.relay_1_timeout = (arg.length() == 0 ? 0 : arg.toInt());
  
  arg = server.arg("relay_2_timeout");
  arg.trim();
  config.relay_2_timeout = (arg.length() == 0 ? 0 : arg.toInt());
  
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
  jsonDocument["relay_1_name"] = config.relay_1_name;
  jsonDocument["relay_1_timeout"] = config.relay_1_timeout;
  jsonDocument["relay_2_name"] = config.relay_2_name;
  jsonDocument["relay_2_timeout"] = config.relay_2_timeout;

  // Serialize JSON to file
  if (serializeJson(jsonDocument, configFile) == 0) {
    Serial.println(F("Failed to write settings file."));
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
	Serial.println("Button 1 has been pressed.");
  toggleRelay(1);
}

void onPressed2() {
	Serial.println("Button 2 has been pressed.");
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
  
  // And attach interrupt watches to meter PINs
  attachInterrupt(digitalPinToInterrupt(PinMeter1), meter1_triggered, FALLING);
  attachInterrupt(digitalPinToInterrupt(PinMeter2), meter2_triggered, FALLING);

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

  // init values from file system
  if (SPIFFS.begin()) {
    Serial.println("File system is mounted.");

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
        config.relay_1_name = json["relay_1_name"].as<String>();
        if(config.relay_1_name.length() == 0) {
          config.relay_1_name = "Relay 1";
        }
        config.relay_2_name = json["relay_2_name"].as<String>();
        if(config.relay_2_name.length() == 0) {
          config.relay_2_name = "Relay 2";
        }
        config.relay_1_timeout = json["relay_1_timeout"] | 0;
        config.relay_2_timeout = json["relay_2_timeout"] | 0;

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
  server.on("/style.css", handle_stylesheetFile);
  server.on("/scripts.js", handle_jsFile);
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

  // Process relay timeouts
  if(config.relay_1_timeout > 0 && relay1timeoutWhen > 0 && relay1timeoutWhen < millis()) {
    Serial.println("Configured timeout for relay 1 exceeded -> toggling");
    toggleRelay(1);
  }

  if(config.relay_2_timeout > 0 && relay2timeoutWhen > 0 && relay2timeoutWhen < millis()) {
    Serial.println("Configured timeout for relay 2 exceeded -> toggling");
    toggleRelay(2);
  }

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
    flowRate2 = ((1000.0 / (millis() - oldTime)) * pulseCounterMeter2) / calibrationFactor;

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

    if(flowRate1 > 0) {
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
    }

    if(flowRate2) {
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
    }

    if(mqttClient.connected()) {
      if(flowRate1 > 0) {
        String channelCurrent = String(config.mqtt_channel + "1/currentFlow");
        String valueCurrent = String(flowRate1);
        mqttClient.publish(channelCurrent.c_str(), valueCurrent.c_str());

        String channelTotal = String(config.mqtt_channel + "1/totalFlow");
        String valueTotal = String(totalMilliLitres1);
        mqttClient.publish(channelTotal.c_str(), valueTotal.c_str());
      }

      if(flowRate2 > 0) {
        String channelCurrent = String(config.mqtt_channel + "2/currentFlow");
        String valueCurrent = String(flowRate2);
        mqttClient.publish(channelCurrent.c_str(), valueCurrent.c_str());

        String channelTotal = String(config.mqtt_channel + "2/totalFlow");
        String valueTotal = String(totalMilliLitres2);
        mqttClient.publish(channelTotal.c_str(), valueTotal.c_str());
      }
    }

    // Reset the pulse counter so we can start incrementing again
    pulseCounterMeter1 = 0;
    pulseCounterMeter2 = 0;

    // Enable the interrupt again now that we've finished sending output
    attachInterrupt(digitalPinToInterrupt(PinMeter1), meter1_triggered, FALLING);
    attachInterrupt(digitalPinToInterrupt(PinMeter2), meter2_triggered, FALLING);
  } 
}
