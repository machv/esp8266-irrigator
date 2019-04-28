#include <Arduino.h>
#include <WiFiManager.h> 

void setup() {
  // put your setup code here, to run once:
  WiFiManager wifiManager;
  wifiManager.autoConnect("AP-NAME");
}

void loop() {
  // put your main code here, to run repeatedly:
}