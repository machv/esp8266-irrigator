# Zavlažovač

## MQTT
```
MQTT_PREFIX = zavlazovac/

LWT = {MQTT_PREFIX}/LWT
	Payload available: "Online"
	Payload not available: "Offline"

State = {MQTT_PREFIX}/state/{INDEX}
Command = {MQTT_PREFIX}/command/{INDEX}/power 
	Payload on: "ON"
	Payload off: "OFF"
```
