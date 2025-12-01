#include "now_mqtt_bridge.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <ArduinoJson.h>

namespace esphome
{
    namespace now_mqtt_bridge
    {
        static const char *const TAG = "now_mqtt_bridge";
        
        // Static instance pointer
        Now_MQTT_BridgeComponent *Now_MQTT_BridgeComponent::instance_ = nullptr;

        // =============================================================================
        // Lifecycle Methods
        // =============================================================================

        float Now_MQTT_BridgeComponent::get_setup_priority() const 
        { 
            return setup_priority::AFTER_CONNECTION; 
        }

        void Now_MQTT_BridgeComponent::setup()
        {
            ESP_LOGD(TAG, "Setting up ESP-NOW MQTT Bridge...");
            
            instance_ = this;

#ifndef USE_WIFI
            // If no WiFi component, initialize WiFi ourselves
            ESP_LOGD(TAG, "Initializing WiFi for ESP-NOW (no WiFi component)...");
            ESP_ERROR_CHECK(esp_netif_init());
            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            ESP_ERROR_CHECK(esp_wifi_init(&cfg));
            ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            ESP_ERROR_CHECK(esp_wifi_start());
            ESP_ERROR_CHECK(esp_wifi_set_channel(this->wifi_channel_, WIFI_SECOND_CHAN_NONE));
#endif

            // Set AP+STA mode for ESP-NOW reception while connected to WiFi
            esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set APSTA mode: %s", esp_err_to_name(err));
                this->mark_failed();
                return;
            }

            // Initialize ESP-NOW
            if (esp_now_init() != ESP_OK) {
                ESP_LOGE(TAG, "Failed to initialize ESP-NOW");
                this->mark_failed();
                return;
            }

            // Register receive callback
            esp_now_register_recv_cb(Now_MQTT_BridgeComponent::static_receive_callback_);

            ESP_LOGI(TAG, "ESP-NOW MQTT Bridge initialized (channel=%d, availability=%s)",
                     this->wifi_channel_, 
                     this->publish_availability_ ? "yes" : "no");
        }

        void Now_MQTT_BridgeComponent::loop()
        {
            // Periodically check for device timeouts
            static uint32_t last_check = 0;
            uint32_t now = millis();
            
            if (now - last_check > 60000) {  // Check every minute
                last_check = now;
                this->check_device_timeouts_();
            }
        }

        // =============================================================================
        // Static Callback
        // =============================================================================

        void Now_MQTT_BridgeComponent::static_receive_callback_(const uint8_t *mac, const uint8_t *data, int len)
        {
            if (instance_ != nullptr) {
                instance_->on_espnow_receive_(mac, data, len);
            }
        }

        // =============================================================================
        // ESP-NOW Receive Handler
        // =============================================================================

        void Now_MQTT_BridgeComponent::on_espnow_receive_(const uint8_t *mac, const uint8_t *data, int len)
        {
            // Convert MAC to string
            std::string mac_str = this->mac_to_string_(mac);
            
            // Copy data to null-terminated buffer
            char received_string[251];
            size_t copy_len = std::min(len, 250);
            memcpy(received_string, data, copy_len);
            received_string[copy_len] = '\0';

            // Tokenize the received string
            char *tokens[13];
            int token_count = split_string_(tokens, 13, received_string, FIELD_DELIMITER);

            // Validate token count
            if (token_count != EXPECTED_TOKEN_COUNT) {
                ESP_LOGD(TAG, "Ignoring malformed packet (got %d tokens, expected %d)", 
                         token_count, EXPECTED_TOKEN_COUNT);
                return;
            }

            ESP_LOGD(TAG, "Received from %s: %s:%s:%s:%s:%s:%s:...", 
                     mac_str.c_str(), tokens[0], tokens[1], tokens[2], tokens[3], tokens[4], tokens[5]);

            // Update device tracking
            if (strlen(tokens[0]) > 0) {
                this->update_device_seen_(mac_str, tokens[0]);
            }

            // Determine message type and process
            std::string message_type = tokens[2];
            
            if (message_type == "binary_sensor") {
                this->process_binary_sensor_message_((const char**)tokens, mac_str);
            } else {
                this->process_sensor_message_((const char**)tokens, mac_str);
            }
        }

        // =============================================================================
        // Message Processing
        // =============================================================================

        void Now_MQTT_BridgeComponent::process_sensor_message_(const char *tokens[], const std::string &mac_str)
        {
            this->publish_sensor_discovery_(tokens, mac_str);
            this->publish_sensor_state_(tokens);
        }

        void Now_MQTT_BridgeComponent::process_binary_sensor_message_(const char *tokens[], const std::string &mac_str)
        {
            this->publish_binary_sensor_discovery_(tokens, mac_str);
            this->publish_binary_sensor_state_(tokens);
        }

        // =============================================================================
        // MQTT Publishing - Sensor
        // =============================================================================

        void Now_MQTT_BridgeComponent::publish_sensor_discovery_(const char *tokens[], const std::string &mac_str)
        {
            DynamicJsonDocument doc(512);
            
            if (strlen(tokens[1]) > 0) doc["dev_cla"] = tokens[1];
            if (strlen(tokens[4]) > 0) doc["unit_of_meas"] = tokens[4];
            if (strlen(tokens[2]) > 0) doc["stat_cla"] = tokens[2];
            if (strlen(tokens[3]) > 0) doc["name"] = tokens[3];
            
            // Icon (reconstruct from split parts)
            if (strlen(tokens[6]) > 0 && strlen(tokens[7]) > 0) {
                std::string icon = std::string(tokens[6]) + ":" + tokens[7];
                doc["icon"] = icon;
            }
            
            // State topic
            std::string state_topic = std::string(tokens[0]) + "/sensor/" + tokens[3] + "/state";
            doc["stat_t"] = state_topic;
            
            // Unique ID
            std::string unique_id = mac_str + "_" + tokens[3];
            doc["uniq_id"] = unique_id;
            
            // Device info
            JsonObject dev = doc["dev"].to<JsonObject>();
            dev["ids"] = mac_str;
            if (strlen(tokens[0]) > 0) dev["name"] = tokens[0];
            dev["sw"] = tokens[8];
            dev["mdl"] = tokens[9];
            dev["mf"] = "espressif";
            
            // Serialize and publish
            std::string json;
            serializeJson(doc, json);
            
            this->discovery_info_ = mqtt::global_mqtt_client->get_discovery_info();
            std::string config_topic = this->discovery_info_.prefix + "/sensor/" + tokens[0] + "/" + tokens[3] + "/config";
            
            mqtt::global_mqtt_client->publish(config_topic, json, 2, true);
            ESP_LOGD(TAG, "Published discovery: %s", config_topic.c_str());
        }

        void Now_MQTT_BridgeComponent::publish_sensor_state_(const char *tokens[])
        {
            std::string state_topic = std::string(tokens[0]) + "/sensor/" + tokens[3] + "/state";
            mqtt::global_mqtt_client->publish(state_topic, tokens[5], 2, true);
            ESP_LOGD(TAG, "Published state: %s = %s", state_topic.c_str(), tokens[5]);
        }

        // =============================================================================
        // MQTT Publishing - Binary Sensor
        // =============================================================================

        void Now_MQTT_BridgeComponent::publish_binary_sensor_discovery_(const char *tokens[], const std::string &mac_str)
        {
            DynamicJsonDocument doc(512);
            
            if (strlen(tokens[3]) > 0) doc["name"] = tokens[3];
            if (strlen(tokens[1]) > 0) doc["dev_cla"] = tokens[1];
            
            // State topic
            std::string state_topic = std::string(tokens[0]) + "/binary_sensor/" + tokens[3] + "/state";
            doc["stat_t"] = state_topic;
            
            // Unique ID
            std::string unique_id = mac_str + "_" + tokens[3];
            doc["uniq_id"] = unique_id;
            
            // Device info
            JsonObject dev = doc["dev"].to<JsonObject>();
            dev["ids"] = mac_str;
            if (strlen(tokens[0]) > 0) dev["name"] = tokens[0];
            dev["sw"] = tokens[8];
            dev["mdl"] = tokens[9];
            dev["mf"] = "espressif";
            
            // Serialize and publish
            std::string json;
            serializeJson(doc, json);
            
            this->discovery_info_ = mqtt::global_mqtt_client->get_discovery_info();
            std::string config_topic = this->discovery_info_.prefix + "/binary_sensor/" + tokens[0] + "/" + tokens[3] + "/config";
            
            mqtt::global_mqtt_client->publish(config_topic, json, 2, true);
        }

        void Now_MQTT_BridgeComponent::publish_binary_sensor_state_(const char *tokens[])
        {
            std::string state_topic = std::string(tokens[0]) + "/binary_sensor/" + tokens[3] + "/state";
            mqtt::global_mqtt_client->publish(state_topic, tokens[5], 2, true);
        }

        // =============================================================================
        // Device Tracking
        // =============================================================================

        void Now_MQTT_BridgeComponent::update_device_seen_(const std::string &mac_str, const std::string &name)
        {
            auto it = this->devices_.find(mac_str);
            
            if (it == this->devices_.end()) {
                // New device
                DeviceInfo info;
                info.name = name;
                info.mac_str = mac_str;
                info.last_seen_ms = millis();
                info.online = true;
                
                this->devices_[mac_str] = info;
                
                ESP_LOGI(TAG, "New device discovered: %s (%s)", name.c_str(), mac_str.c_str());
                
                if (this->publish_availability_) {
                    this->publish_device_availability_(name, true);
                }
            } else {
                // Existing device
                bool was_offline = !it->second.online;
                
                it->second.last_seen_ms = millis();
                it->second.online = true;
                
                if (was_offline) {
                    ESP_LOGI(TAG, "Device back online: %s", name.c_str());
                    if (this->publish_availability_) {
                        this->publish_device_availability_(name, true);
                    }
                }
            }
        }

        void Now_MQTT_BridgeComponent::check_device_timeouts_()
        {
            uint32_t now = millis();
            
            for (auto &pair : this->devices_) {
                DeviceInfo &info = pair.second;
                
                if (info.online && (now - info.last_seen_ms) > DEVICE_TIMEOUT_MS) {
                    info.online = false;
                    ESP_LOGW(TAG, "Device offline: %s (no packets for %u ms)", 
                             info.name.c_str(), DEVICE_TIMEOUT_MS);
                    
                    if (this->publish_availability_) {
                        this->publish_device_availability_(info.name, false);
                    }
                }
            }
        }

        void Now_MQTT_BridgeComponent::publish_device_availability_(const std::string &device_name, bool online)
        {
            std::string topic = device_name + "/status";
            std::string payload = online ? "online" : "offline";
            mqtt::global_mqtt_client->publish(topic, payload, 2, true);
        }

        // =============================================================================
        // Utility Methods
        // =============================================================================

        std::string Now_MQTT_BridgeComponent::mac_to_string_(const uint8_t *mac)
        {
            char mac_str[18];
            snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            return std::string(mac_str);
        }

        int Now_MQTT_BridgeComponent::split_string_(char **tokens, int max_tokens, char *string, char delimiter)
        {
            int count = 0;
            char *token = string;
            
            while (*string && count < max_tokens) {
                if (*string == delimiter) {
                    *string = '\0';
                    tokens[count++] = token;
                    token = string + 1;
                }
                string++;
            }
            
            // Add the last token
            if (count < max_tokens) {
                tokens[count++] = token;
            }
            
            return count;
        }

    } // namespace now_mqtt_bridge
} // namespace esphome
