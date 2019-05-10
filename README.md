# Simple Irrigator controller

HW Parts used:

  - NodeMCU v0.9
  - Two Channel IIC I2C Logic Level Converter Bi-Directional Module 5V to 3.3V NEW
  - Full Port Brass DN20 Normally Open Normally Close Valve AC/DC9V-24V BSP/NPT 3/4'' Electric Shut Off Valve For Home Water (https://www.aliexpress.com/item/AC-DC9-24V-3-4-full-port-brass-electric-ball-valve-normal-open-or-normal-closed/886318623.html)
  - G1/2"/G3/4" Copper Hall Effect Liquid Water Flow Sensor Switch Flowmeter Meter J [G 3/4"]  
  - 5V Two 2 Channel Relay Module With optocoupler For PIC AVR DSP ARM Arduino

## MQTT

| Variable | Example | Meaning | 
| -------- | ------- | ------- |
| `MQTT_PREFIX` | `zavlazovac/` | Left prefix appended to relay's channel, configurable in web interface. |
| `RELAY_INDEX` | `1` | Index of the relay, numbering starts from 1 |

### LWT channel

Last will topic is used to detect availability, messages sent there are with retain tag enabled.
```
{MQTT_PREFIX}/LWT
```
| Payload | Value | Retain |
| ------   | --- | -- |
| Available     | `Online` | yes |
| Not Available | `Offline` | sent as LTW |

### State updates channel

State topic publishes current status of the relays.

```
{MQTT_PREFIX}/state/{RELAY_INDEX}
```

| Payload | Value | Retain |
| ------   | --- | -- |
| Running     | `1` | no |
| Not running | `0` | no |

### Commands channel

Command topic accepts these state request changes and accepts both string `ON`, `OFF` or interger `1`, `0`.

```
{MQTT_PREFIX}/command/{RELAY_INDEX}/power
```

| Payload | Value |
| ------   | --- |
| Running     | `1` or `ON`  |
| Not running | `0` or `OFF` |
