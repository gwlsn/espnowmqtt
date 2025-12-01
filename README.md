# ESP-NOW MQTT for ESPHome

A battery-friendly ESP-NOW to MQTT bridge for ESPHome, enabling low-power sensors to communicate with Home Assistant without the overhead of Wi-Fi.

## What This Does

This library lets you build battery-powered sensors that last months on a single charge by using ESP-NOW instead of Wi-Fi. It consists of two components:

- **now_mqtt** — Runs on battery-powered sensor nodes. Wakes from deep sleep, reads sensors, broadcasts data via ESP-NOW, goes back to sleep. No Wi-Fi stack, no DHCP, no TCP overhead.

- **now_mqtt_bridge** — Runs on a mains-powered ESP32 connected to your network. Receives ESP-NOW broadcasts and publishes them to MQTT with Home Assistant auto-discovery.

```
┌─────────────────┐        ESP-NOW         ┌─────────────────┐        MQTT         ┌─────────────────┐
│  Battery Node   │ ~~~~~~~~~~~~~~~~~~~~>  │     Bridge      │ ───────────────────>│ Home Assistant  │
│  (Deep Sleep)   │      (No Wi-Fi)        │  (Mains Power)  │      (Wi-Fi)        │                 │
└─────────────────┘                        └─────────────────┘                     └─────────────────┘
```

## Attribution & Credits

This is a fork of [Microfire's ESPHomeComponents](https://github.com/u-fire/ESPHomeComponents), specifically the `now_mqtt` and `now_mqtt_bridge` components.

**Original work by:** [Microfire LLC](https://github.com/u-fire)  
**Original article:** [ESP-NOW & MQTT with ESPHome](https://microfire.co/esp-now-mqtt-esphome/)

## AI Disclosure

The improvements in this fork were developed with assistance from Claude Code.

## Improvements Over Original

| Feature | Original | This Fork |
|---------|----------|-----------|
| **Delivery confirmation** | Fire-and-forget | Send callback with retry logic (up to 3 attempts) |
| **Wi-Fi channel** | Documented as channel 1 only | Configurable via YAML (1-14) |
| **Long-range mode** | Always on | Configurable via YAML |
| **Error handling** | `ESP_ERROR_CHECK` (crashes on failure) | Graceful logging, failure callbacks |
| **Device availability** | None | Bridge publishes offline status if no packets received within timeout period (default 5 min) |
| **Send result triggers** | None | `on_send_success` / `on_send_failure` automations |

### Bug Fixes

- `wifi_channel_` member variable is now initialized (was undefined)
- ESP8266 now respects the channel configuration
- Fixed static callback pattern for ESP-NOW receive handlers

## Installation

Add to your ESPHome YAML:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/YOUR_USERNAME/YOUR_REPO_NAME
    components: [now_mqtt]  # or [now_mqtt_bridge]
```

## Usage

### Sensor Node (Battery-Powered)

```yaml
esphome:
  name: sensor-node

esp32:
  board: firebeetle32

external_components:
  - source:
      type: git
      url: https://github.com/YOUR_USERNAME/YOUR_REPO_NAME
    components: [now_mqtt]

# No wifi: block — this node is ESP-NOW only
now_mqtt:
  wifi_channel: 6           # Must match your 2.4GHz AP channel
  long_range_mode: true     # Optional, default true
  on_send_failure:
    - logger.log: "Send failed after retries"

i2c:
  sda: GPIO21
  scl: GPIO22

sensor:
  - platform: bme280_i2c
    temperature:
      name: "Temperature"
    humidity:
      name: "Humidity"

deep_sleep:
  run_duration: 10s
  sleep_duration: 10min
```

### Bridge Node (Mains-Powered)

```yaml
esphome:
  name: espnow-bridge

esp32:
  board: esp32dev

external_components:
  - source:
      type: git
      url: https://github.com/YOUR_USERNAME/YOUR_REPO_NAME
    components: [now_mqtt_bridge]

now_mqtt_bridge:
  wifi_channel: 6           # Only used if wifi: not present
  publish_rssi: true        # Create RSSI sensor for each device
  publish_availability: true # Publish online/offline status

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

mqtt:
  broker: homeassistant.local
  username: !secret mqtt_username
  password: !secret mqtt_password
```

## Configuration Options

### now_mqtt (Sensor Node)

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `wifi_channel` | int | 1 | ESP-NOW channel (1-14). Must match bridge/AP. |
| `long_range_mode` | bool | true | Enable Espressif LR protocol for extended range. |
| `on_sent` | automation | — | Trigger when data is sent (legacy). |
| `on_send_success` | automation | — | Trigger when send confirmed successful. |
| `on_send_failure` | automation | — | Trigger when send fails after all retries. |

### now_mqtt_bridge (Bridge Node)

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `wifi_channel` | int | 1 | Fallback channel if `wifi:` component not used. |
| `publish_availability` | bool | true | Publish online/offline status (5 min timeout). |

## Important Notes

### Channel Configuration

ESP-NOW requires all devices to be on the same 2.4GHz channel. When your bridge is connected to Wi-Fi, it's locked to whatever channel your access point uses.

1. Check which channel your 2.4GHz AP uses (or set it manually in your router)
2. Use that same channel number in `wifi_channel` for your sensor nodes
3. Channels 1, 6, or 11 recommended (non-overlapping)

### Firmware Updates

The sensor node has no Wi-Fi stack — **OTA updates are not possible**. You'll need USB access to reflash. This is an intentional tradeoff for battery life.

The bridge node supports OTA normally since it's always connected to Wi-Fi.

### Long Range Mode

When `long_range_mode: true`, the sensor uses Espressif's proprietary LR protocol. This extends range significantly but:

- Both ends must be ESP32 (no ESP8266)
- Throughput is reduced (doesn't matter for sensors)
- Both sender and receiver must have it enabled

## License

This project inherits the license from the original Microfire repository. See [LICENSE](LICENSE) for details.
