#pragma once

#include "esphome/core/component.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include <map>
#include <string>

namespace esphome
{
    namespace now_mqtt_bridge
    {
        // =============================================================================
        // Constants
        // =============================================================================
        static constexpr char FIELD_DELIMITER = ':';
        static constexpr uint8_t EXPECTED_TOKEN_COUNT = 11;
        static constexpr uint32_t DEVICE_TIMEOUT_MS = 300000;  // 5 minutes

        // =============================================================================
        // Device Tracking
        // =============================================================================
        struct DeviceInfo {
            std::string name;
            std::string mac_str;
            uint32_t last_seen_ms;
            bool online;
        };

        // =============================================================================
        // Main Component Class
        // =============================================================================
        class Now_MQTT_BridgeComponent : public Component
        {
        public:
            void setup() override;
            void loop() override;
            float get_setup_priority() const override;

            // Configuration setters
            void set_wifi_channel(uint8_t channel) { this->wifi_channel_ = channel; }
            void set_publish_availability(bool enabled) { this->publish_availability_ = enabled; }

        protected:
            uint8_t wifi_channel_ = 1;
            bool publish_availability_ = true;

        private:
            // Device tracking
            std::map<std::string, DeviceInfo> devices_;
            
            // MQTT discovery info cache
            mqtt::MQTTDiscoveryInfo discovery_info_;

            // Callback handlers
            void on_espnow_receive_(const uint8_t *mac, const uint8_t *data, int len);
            static void static_receive_callback_(const uint8_t *mac, const uint8_t *data, int len);

            // Message processing
            void process_sensor_message_(const char *tokens[], const std::string &mac_str);
            void process_binary_sensor_message_(const char *tokens[], const std::string &mac_str);

            // MQTT publishing
            void publish_sensor_discovery_(const char *tokens[], const std::string &mac_str);
            void publish_sensor_state_(const char *tokens[]);
            void publish_binary_sensor_discovery_(const char *tokens[], const std::string &mac_str);
            void publish_binary_sensor_state_(const char *tokens[]);
            void publish_device_availability_(const std::string &device_name, bool online);

            // Device tracking
            void update_device_seen_(const std::string &mac_str, const std::string &name);
            void check_device_timeouts_();
            std::string mac_to_string_(const uint8_t *mac);

            // String parsing
            static int split_string_(char **tokens, int max_tokens, char *string, char delimiter);

            // Static instance for callbacks
            static Now_MQTT_BridgeComponent *instance_;
        };

    } // namespace now_mqtt_bridge
} // namespace esphome
