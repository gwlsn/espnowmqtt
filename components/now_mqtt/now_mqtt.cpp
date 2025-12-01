#include "now_mqtt.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/core/version.h"
#include <esphome/core/helpers.h>

#ifdef USE_ESP32
#include "Arduino.h"
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#endif

#ifdef USE_ESP8266
#include "Arduino.h"
#include <espnow.h>
#include <ESP8266WiFi.h>
#endif

namespace esphome
{
    namespace now_mqtt
    {
        static const char *const TAG = "now_mqtt";
        
        // Static instance pointer for ESP-NOW callbacks
        Now_MQTTComponent *Now_MQTTComponent::instance_ = nullptr;

        // =============================================================================
        // Lifecycle Methods
        // =============================================================================

        float Now_MQTTComponent::get_setup_priority() const 
        { 
            return setup_priority::AFTER_WIFI; 
        }

        void Now_MQTTComponent::setup()
        {
            ESP_LOGD(TAG, "Setting up ESP-NOW MQTT component...");
            
            instance_ = this;
            
            this->init_esp_now_();
            
            if (this->is_failed()) {
                return;
            }
            
            this->register_sensor_callbacks_();
            
            ESP_LOGI(TAG, "ESP-NOW MQTT initialized (channel=%d, long_range=%s)",
                     this->wifi_channel_, this->long_range_mode_ ? "yes" : "no");
        }

        void Now_MQTTComponent::loop()
        {
            // Nothing to do in loop - we send on sensor updates
        }

        // =============================================================================
        // Initialization Helpers
        // =============================================================================

        void Now_MQTTComponent::init_esp_now_()
        {
#ifdef USE_ESP32
            uint8_t broadcast_address[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            esp_now_peer_info_t peer_info = {};

            // Initialize WiFi stack (without connecting to AP)
            esp_err_t err;
            
            err = esp_netif_init();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
                this->mark_failed();
                return;
            }
            
            err = esp_event_loop_create_default();
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
                this->mark_failed();
                return;
            }
            
            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            err = esp_wifi_init(&cfg);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
                this->mark_failed();
                return;
            }
            
            ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            ESP_ERROR_CHECK(esp_wifi_start());
            ESP_ERROR_CHECK(esp_wifi_set_channel(this->wifi_channel_, WIFI_SECOND_CHAN_NONE));

            // Initialize ESP-NOW
            if (esp_now_init() != ESP_OK) {
                ESP_LOGE(TAG, "esp_now_init failed");
                this->mark_failed();
                return;
            }

            // Register send callback for delivery confirmation
            esp_now_register_send_cb(Now_MQTTComponent::send_callback_);

            // Set long range mode if enabled
            if (this->long_range_mode_) {
                esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
                ESP_LOGD(TAG, "Long range mode enabled");
            }

            // Add broadcast peer
            memcpy(peer_info.peer_addr, broadcast_address, 6);
            peer_info.channel = this->wifi_channel_;
            peer_info.encrypt = false;

            if (esp_now_add_peer(&peer_info) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to add broadcast peer");
                this->mark_failed();
                return;
            }
#endif

#ifdef USE_ESP8266
            uint8_t broadcast_address[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            
            WiFi.mode(WIFI_STA);
            WiFi.disconnect();
            
            if (esp_now_init() != 0) {
                ESP_LOGE(TAG, "esp_now_init failed");
                this->mark_failed();
                return;
            }
            
            esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
            esp_now_add_peer(broadcast_address, ESP_NOW_ROLE_COMBO, this->wifi_channel_, NULL, 0);
#endif
        }

        void Now_MQTTComponent::register_sensor_callbacks_()
        {
            // Register callbacks for all sensors
            for (auto *obj : App.get_sensors()) {
                obj->add_on_state_callback([this, obj](float state) {
                    this->on_sensor_update(obj, state);
                });
            }

#ifdef USE_BINARY_SENSOR
            for (auto *obj : App.get_binary_sensors()) {
                obj->add_on_state_callback([this, obj](bool state) {
                    this->on_binary_sensor_update(obj, state);
                });
            }
#endif

#ifdef USE_TEXT_SENSOR
            for (auto *obj : App.get_text_sensors()) {
                obj->add_on_state_callback([this, obj](std::string state) {
                    this->on_text_sensor_update(obj, state);
                });
            }
#endif
        }

        // =============================================================================
        // Send Methods
        // =============================================================================

        void Now_MQTTComponent::send_callback_(const uint8_t *mac_addr, esp_now_send_status_t status)
        {
            if (instance_ != nullptr) {
                instance_->last_send_success_ = (status == ESP_NOW_SEND_SUCCESS);
                instance_->send_in_progress_ = false;
            }
        }

        bool Now_MQTTComponent::send_with_retry_(const uint8_t *data, size_t len)
        {
            uint8_t broadcast_address[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            
            for (uint8_t attempt = 0; attempt <= MAX_RETRIES; attempt++) {
                if (attempt > 0) {
                    ESP_LOGD(TAG, "Retry attempt %d/%d", attempt, MAX_RETRIES);
                    delay(RETRY_DELAY_MS);
                }
                
                this->send_in_progress_ = true;
                this->last_send_success_ = false;
                
#ifdef USE_ESP32
                esp_err_t result = esp_now_send(broadcast_address, data, len);
                if (result != ESP_OK) {
                    ESP_LOGW(TAG, "esp_now_send failed: %s", esp_err_to_name(result));
                    this->send_in_progress_ = false;
                    continue;
                }
#endif

#ifdef USE_ESP8266
                int result = esp_now_send(broadcast_address, const_cast<uint8_t*>(data), len);
                if (result != 0) {
                    ESP_LOGW(TAG, "esp_now_send failed: %d", result);
                    this->send_in_progress_ = false;
                    continue;
                }
#endif
                
                // Wait for send callback (with timeout)
                uint32_t start = millis();
                while (this->send_in_progress_ && (millis() - start) < 100) {
                    delay(1);
                }
                
                if (this->last_send_success_) {
                    this->send_success_callback_.call();
                    return true;
                }
            }
            
            // All retries failed
            this->send_failure_callback_.call();
            ESP_LOGW(TAG, "Send failed after %d retries", MAX_RETRIES + 1);
            return false;
        }

        // =============================================================================
        // Sensor Update Handlers
        // =============================================================================

        std::string Now_MQTTComponent::build_sensor_string_(sensor::Sensor *obj, float state)
        {
            std::string line;
            int8_t accuracy = obj->get_accuracy_decimals();
            
            line = str_snake_case(App.get_name());
            line += FIELD_DELIMITER;
            line += obj->get_device_class().c_str();
            line += FIELD_DELIMITER;
            line += state_class_to_string(obj->get_state_class()).c_str();
            line += FIELD_DELIMITER;
            line += str_snake_case(obj->get_name().c_str());
            line += FIELD_DELIMITER;
            line += obj->get_unit_of_measurement().c_str();
            line += FIELD_DELIMITER;
            line += value_accuracy_to_string(state, accuracy);
            line += FIELD_DELIMITER;
            
            if (obj->get_icon().length() != 0) {
                line += obj->get_icon();
            } else {
                line += FIELD_DELIMITER;
            }
            
            line += FIELD_DELIMITER;
            line += ESPHOME_VERSION;
            line += FIELD_DELIMITER;
            line += ESPHOME_BOARD;
            line += ":sensor:";
            
            return line;
        }

        void Now_MQTTComponent::on_sensor_update(sensor::Sensor *obj, float state)
        {
            if (!obj->has_state())
                return;

            std::string line = this->build_sensor_string_(obj, state);
            
            ESP_LOGI(TAG, "Publishing: %s", line.c_str());
            
            this->send_with_retry_(reinterpret_cast<const uint8_t *>(line.c_str()), line.size());
            this->callback_.call(state);
        }

#ifdef USE_BINARY_SENSOR
        std::string Now_MQTTComponent::build_binary_sensor_string_(binary_sensor::BinarySensor *obj, bool state)
        {
            std::string line;
            const char *state_s = state ? "ON" : "OFF";

            line = str_snake_case(App.get_name());
            line += FIELD_DELIMITER;
            line += obj->get_device_class().c_str();
            line += FIELD_DELIMITER;
            line += "binary_sensor";
            line += FIELD_DELIMITER;
            line += str_snake_case(obj->get_name().c_str());
            line += FIELD_DELIMITER;
            line += FIELD_DELIMITER;
            line += state_s;
            line += FIELD_DELIMITER;
            
            if (obj->get_icon().length() != 0) {
                line += obj->get_icon();
            } else {
                line += FIELD_DELIMITER;
            }
            
            line += FIELD_DELIMITER;
            line += ESPHOME_VERSION;
            line += FIELD_DELIMITER;
            line += ESPHOME_BOARD;
            line += "::";
            
            return line;
        }

        void Now_MQTTComponent::on_binary_sensor_update(binary_sensor::BinarySensor *obj, float state)
        {
            if (!obj->has_state())
                return;

            std::string line = this->build_binary_sensor_string_(obj, state != 0.0f);
            
            ESP_LOGI(TAG, "Publishing: %s", line.c_str());
            
            this->send_with_retry_(reinterpret_cast<const uint8_t *>(line.c_str()), line.size());
            this->callback_.call(state);
        }
#endif

#ifdef USE_TEXT_SENSOR
        std::string Now_MQTTComponent::build_text_sensor_string_(text_sensor::TextSensor *obj, const std::string &state)
        {
            std::string line;

            line = str_snake_case(App.get_name());
            line += FIELD_DELIMITER;
            line += FIELD_DELIMITER;
            line += FIELD_DELIMITER;
            line += str_snake_case(obj->get_name().c_str());
            line += FIELD_DELIMITER;
            line += FIELD_DELIMITER;
            line += state;
            line += FIELD_DELIMITER;
            
            if (obj->get_icon().length() != 0) {
                line += obj->get_icon();
            } else {
                line += FIELD_DELIMITER;
            }
            
            line += FIELD_DELIMITER;
            line += ESPHOME_VERSION;
            line += FIELD_DELIMITER;
            line += ESPHOME_BOARD;
            line += "::";
            
            return line;
        }

        void Now_MQTTComponent::on_text_sensor_update(text_sensor::TextSensor *obj, std::string state)
        {
            if (!obj->has_state())
                return;

            std::string line = this->build_text_sensor_string_(obj, state);
            
            ESP_LOGI(TAG, "Publishing: %s", line.c_str());
            
            this->send_with_retry_(reinterpret_cast<const uint8_t *>(line.c_str()), line.size());
            this->callback_.call(0.0f);
        }
#endif

    } // namespace now_mqtt
} // namespace esphome
