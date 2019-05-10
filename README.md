# Simple Irrigator controller

## MQTT
```
MQTT_PREFIX = zavlazovac/
```

Last will topic is used to detect availability, messages sent there are with retain tag enabled.
```
LWT = {MQTT_PREFIX}/LWT
	Payload available: "Online"
	Payload not available: "Offline"
```
```
State = {MQTT_PREFIX}/state/{INDEX}
Command = {MQTT_PREFIX}/command/{INDEX}/power 
	Payload on: "ON"
	Payload off: "OFF"
```
