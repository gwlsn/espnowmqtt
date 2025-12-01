import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import (
    CONF_ID,
    CONF_TRIGGER_ID,
)
from esphome.core import coroutine_with_priority

# =============================================================================
# Configuration Keys
# =============================================================================
CONF_CHANNEL = "wifi_channel"
CONF_LONG_RANGE = "long_range_mode"
CONF_ON_SEND = "on_sent"
CONF_ON_SEND_SUCCESS = "on_send_success"
CONF_ON_SEND_FAILURE = "on_send_failure"

# =============================================================================
# C++ Class References
# =============================================================================
now_mqtt_ns = cg.esphome_ns.namespace("now_mqtt")

Now_MQTTComponent = now_mqtt_ns.class_(
    "Now_MQTTComponent", cg.Component
)

# Triggers
ESPNowSendTrigger = now_mqtt_ns.class_(
    "ESPNowSendTrigger", automation.Trigger.template(cg.float_)
)
ESPNowSendSuccessTrigger = now_mqtt_ns.class_(
    "ESPNowSendSuccessTrigger", automation.Trigger.template()
)
ESPNowSendFailureTrigger = now_mqtt_ns.class_(
    "ESPNowSendFailureTrigger", automation.Trigger.template()
)

# =============================================================================
# Configuration Schema
# =============================================================================
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Now_MQTTComponent),
    
    # WiFi channel (1-14, default 1)
    cv.Optional(CONF_CHANNEL, default=1): cv.int_range(min=1, max=14),
    
    # Long range mode (default true for backward compatibility)
    cv.Optional(CONF_LONG_RANGE, default=True): cv.boolean,
    
    # Automation triggers
    cv.Optional(CONF_ON_SEND): automation.validate_automation({
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ESPNowSendTrigger),
    }),
    cv.Optional(CONF_ON_SEND_SUCCESS): automation.validate_automation({
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ESPNowSendSuccessTrigger),
    }),
    cv.Optional(CONF_ON_SEND_FAILURE): automation.validate_automation({
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ESPNowSendFailureTrigger),
    }),
})

# =============================================================================
# Code Generation
# =============================================================================
@coroutine_with_priority(40.0)
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    # Set configuration options
    cg.add(var.set_wifi_channel(config[CONF_CHANNEL]))
    cg.add(var.set_long_range_mode(config[CONF_LONG_RANGE]))
    
    # Build automation triggers
    for conf in config.get(CONF_ON_SEND, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.float_, "x")], conf)
    
    for conf in config.get(CONF_ON_SEND_SUCCESS, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
    
    for conf in config.get(CONF_ON_SEND_FAILURE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
