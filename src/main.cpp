#include <FS.h> //this needs to be first
#include <Arduino.h>
#include <WiFiManager.h> 
#include <ESP8266WebServer.h>
//#include <PubSubClient.h>         //MQTT server library

#include <EasyButton.h>

#include <ArduinoJson.h>

//for LED status
#include <Ticker.h>
Ticker ticker;

struct Config {
  String mqtt_server; //[64];
  int mqtt_port;
  char mqtt_user[64];
  char mqtt_password[64];
};

const char *ConfigFileName = "/config.json";
Config config; 

EasyButton button(D3);

ESP8266WebServer server(80);

WiFiClient espClient;
//PubSubClient client(espClient);

//  const int  buttonPin = D2; // variable for D2 pin
//   int contagem = 0;   // variable to store the “rise ups” from the flowmeter pulses


//#define PumpRelay D3
//#define LampRelay D4

void tick()
{
  //toggle state
  //int state = digitalRead(LED_BUILTIN);  // get the current state of GPIO1 pin
  //digitalWrite(LED_BUILTIN, !state);     // set pin to the opposite state

  Serial.println("tick");
}
/*
//Pump Control
void pump_off(){
  digitalWrite(PumpRelay, LOW);
}
void pump_on(){
  digitalWrite(PumpRelay, HIGH);
}
//Lamp Control
void lamp_off(){
  digitalWrite(LampRelay, LOW);
}
void lamp_on(){
  digitalWrite(LampRelay, HIGH);
}
*/
//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}
/*
  //Interrupt function, so that the counting of pulse “rise ups” dont interfere with the rest of the code  (attachInterrupt)
  void pin_ISR()
  {   
      contagem++;
  }
*/

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
  
  ptr +="<form method=\"post\" enctype=\"application/x-www-form-urlencoded\">\n";
  ptr +="<table>\n";
  ptr +="  <tr>\n";
  ptr +="    <td>MQTT Server</td>\n";
  ptr +="    <td><input type=\"text\" name=\"mqtt_server\" value=\"" + (config.mqtt_server) + "\"></td>\n";
  ptr +="  </tr>\n";
  ptr +="  <tr>\n";
  ptr +="    <td colspan=\"2\"><input type=\"submit\" value=\"Save\"></td>\n";
  ptr +="  </tr>\n";
  ptr +="</table>\n";
  ptr +="</form>\n";
  
  ptr +="</body>\n";
  ptr +="</html>\n";
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
  ptr +="<h3>Using Access Point(AP) Mode</h3>\n";
  ptr +="<a href=\"/config\">Configuration</a>\n";
 /* 
   if(led1stat)
  {ptr +="<p>LED1 Status: ON</p><a class=\"button button-off\" href=\"/led1off\">OFF</a>\n";}
  else
  {ptr +="<p>LED1 Status: OFF</p><a class=\"button button-on\" href=\"/led1on\">ON</a>\n";}

  if(led2stat)
  {ptr +="<p>LED2 Status: ON</p><a class=\"button button-off\" href=\"/led2off\">OFF</a>\n";}
  else
  {ptr +="<p>LED2 Status: OFF</p><a class=\"button button-on\" href=\"/led2on\">ON</a>\n";}
*/
  ptr +="</body>\n";
  ptr +="</html>\n";
  return ptr;
}

void handle_pageConfig() {
  Serial.println("GPIO7 Status: OFF | GPIO6 Status: OFF");
  server.send(200, "text/html", SendSettingsHTML()); 
}

void handle_saveConfig() {
  config.mqtt_server = server.arg("mqtt_server"); 

  Serial.println(config.mqtt_server);

  File configFile = SPIFFS.open(ConfigFileName, "w");

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/assistant to compute the capacity.
  StaticJsonDocument<256> doc;

  // Set the values in the document
  doc["mqtt_server"] = config.mqtt_server;

  // Serialize JSON to file
  if (serializeJson(doc, configFile) == 0) {
    Serial.println(F("Failed to write to file"));
  }

  // Close the file
  configFile.close();

  server.send(200, "text/html", "OK"); 
}

void handle_OnConnect() {
  Serial.println("GPIO7 Status: OFF | GPIO6 Status: OFF");
  server.send(200, "text/html", SendHTML()); 
}

void handle_NotFound(){
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

  // !!! using internal LED blocks internal TTY output !!!
   //set led pin as output
  //pinMode(LED_BUILTIN, OUTPUT);
  // start ticker with 0.5 because we start in AP mode and try to connect
  //ticker.attach(0.6, tick);

  // WiFiManager
  WiFiManager wifiManager;
  wifiManager.autoConnect("Zavlazovac-Init");

  // Set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }

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

        //json.printTo(Serial);

          // Copy values from the JsonDocument to the Config
          config.mqtt_port = json["mqtt_port"];
          config.mqtt_server = json["mqtt_server"].as<String>();
          //strlcpy(config.mqtt_server,        // <- destination
          //        json["mqtt_server"],       // <- source
          //        sizeof(config.mqtt_server));  // <- destination's capacity
          //strlcpy(config.mqtt_user,        // <- destination
          //        json["mqtt_user"],       // <- source
          //        sizeof(config.mqtt_user));  // <- destination's capacity
          //strlcpy(config.mqtt_password,        // <- destination
          //        json["mqtt_password"],       // <- source
          //        sizeof(config.mqtt_password));  // <- destination's capacity

        configFile.close();
    }
  }


// water flow sensors
   // Initialization of the variable “buttonPin” as INPUT (D2 pin)
  // pinMode(buttonPin, INPUT);
  //attachInterrupt(digitalPinToInterrupt(buttonPin), pin_ISR, RISING);

// relays
//  pinMode(PumpRelay,OUTPUT);
//  pinMode(LampRelay,OUTPUT);  

    server.on("/", handle_OnConnect);
    server.on("/config", HTTP_GET, handle_pageConfig);
    server.on("/config", HTTP_POST, handle_saveConfig);
  //server.on("/mqtt", handle_mqtt);
  server.onNotFound(handle_NotFound);
  
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
  
  //client.loop();
  
  // Continuously read the status of the button. 
	button.read();
}
