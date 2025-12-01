import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import coroutine_with_priority

# =============================================================================
# Configuration Keys
# =============================================================================
CONF_CHANNEL = "wifi_channel"
CONF_PUBLISH_AVAILABILITY = "publish_availability"

# Ensure MQTT dependency
DEPENDENCIES = ["mqtt"]

# =============================================================================
# C++ Class References
# =============================================================================
now_mqtt_bridge_ns = cg.esphome_ns.namespace("now_mqtt_bridge")

Now_MQTT_BridgeComponent = now_mqtt_bridge_ns.class_(
    "Now_MQTT_BridgeComponent", cg.Component
)

# =============================================================================
# Configuration Schema
# =============================================================================
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Now_MQTT_BridgeComponent),
    
    # WiFi channel (1-14, default 1)
    # NOTE: This is only used if wifi: component is not present.
    # If wifi: is configured, the bridge uses that channel automatically.
    cv.Optional(CONF_CHANNEL, default=1): cv.int_range(min=1, max=14),
    
    # Publish availability (online/offline) for each device (default true)
    cv.Optional(CONF_PUBLISH_AVAILABILITY, default=True): cv.boolean,
})

# =============================================================================
# Code Generation
# =============================================================================
@coroutine_with_priority(1.0)  # Low priority = runs after MQTT is ready
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    cg.add(var.set_wifi_channel(config[CONF_CHANNEL]))
    cg.add(var.set_publish_availability(config[CONF_PUBLISH_AVAILABILITY]))
