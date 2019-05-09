#include <Arduino.h>

#define RELAYS_COUNT 2

struct RelayConfiguration {
  String name;
  int timeout;    
};

struct Configuration {
  String mqtt_server;
  int mqtt_port;
  String mqtt_user;
  String mqtt_password;
  String mqtt_channel;

  RelayConfiguration relays[RELAYS_COUNT];
};
