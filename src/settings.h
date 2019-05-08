#include <Arduino.h>

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
