#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/automation.h"

#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif

#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

namespace esphome
{
    namespace now_mqtt
    {
        // =============================================================================
        // Constants
        // =============================================================================
        static constexpr uint8_t MAX_RETRIES = 2;
        static constexpr uint8_t RETRY_DELAY_MS = 10;
        static constexpr char FIELD_DELIMITER = ':';

        // =============================================================================
        // Main Component Class
        // =============================================================================
        class Now_MQTTComponent : public Component
        {
        public:
            void setup() override;
            void loop() override;
            float get_setup_priority() const override;

            // Configuration setters (called from Python codegen)
            void set_wifi_channel(uint8_t channel) { this->wifi_channel_ = channel; }
            void set_long_range_mode(bool enabled) { this->long_range_mode_ = enabled; }

            // Callback registration
            void add_on_state_callback(std::function<void(float)> callback) { this->callback_.add(callback); }
            void add_on_send_success_callback(std::function<void()> callback) { this->send_success_callback_.add(callback); }
            void add_on_send_failure_callback(std::function<void()> callback) { this->send_failure_callback_.add(callback); }

        protected:
            // Configuration
            uint8_t wifi_channel_ = 1;
            bool long_range_mode_ = true;

            // State
            volatile bool send_in_progress_ = false;
            volatile bool last_send_success_ = false;

        private:
            // Callback managers
            CallbackManager<void(float)> callback_;
            CallbackManager<void()> send_success_callback_;
            CallbackManager<void()> send_failure_callback_;

            // Initialization helpers
            void init_esp_now_();
            void register_sensor_callbacks_();

            // Send methods
            bool send_with_retry_(const uint8_t *data, size_t len);
            static void send_callback_(const uint8_t *mac_addr, esp_now_send_status_t status);

            // Sensor update handlers
            void on_sensor_update(sensor::Sensor *obj, float state);
            std::string build_sensor_string_(sensor::Sensor *obj, float state);

#ifdef USE_BINARY_SENSOR
            void on_binary_sensor_update(binary_sensor::BinarySensor *obj, float state);
            std::string build_binary_sensor_string_(binary_sensor::BinarySensor *obj, bool state);
#endif

#ifdef USE_TEXT_SENSOR
            void on_text_sensor_update(text_sensor::TextSensor *obj, std::string state);
            std::string build_text_sensor_string_(text_sensor::TextSensor *obj, const std::string &state);
#endif

            // Static instance pointer for callbacks
            static Now_MQTTComponent *instance_;
        };

        // =============================================================================
        // Automation Triggers
        // =============================================================================
        class ESPNowSendTrigger : public Trigger<float>
        {
        public:
            explicit ESPNowSendTrigger(Now_MQTTComponent *parent)
            {
                parent->add_on_state_callback([this](float value) { this->trigger(value); });
            }
        };

        class ESPNowSendSuccessTrigger : public Trigger<>
        {
        public:
            explicit ESPNowSendSuccessTrigger(Now_MQTTComponent *parent)
            {
                parent->add_on_send_success_callback([this]() { this->trigger(); });
            }
        };

        class ESPNowSendFailureTrigger : public Trigger<>
        {
        public:
            explicit ESPNowSendFailureTrigger(Now_MQTTComponent *parent)
            {
                parent->add_on_send_failure_callback([this]() { this->trigger(); });
            }
        };

    } // namespace now_mqtt
} // namespace esphome
