import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID, CONF_UART_ID

# Create a namespace matching the C++ namespace (esphome::midea_dehum)
midea_dehum_ns = cg.esphome_ns.namespace("midea_dehum")
# The C++ class name must match exactly
MideaDehumComponent = midea_dehum_ns.class_(
    "MideaDehumComponent", cg.Component, uart.UARTDevice
)

# YAML schema: require an id and a uart_id
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MideaDehumComponent),
        cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    uart_comp = await cg.get_variable(config[CONF_UART_ID])
    var = cg.new_Pvariable(config[CONF_ID], uart_comp)
    await cg.register_component(var, config)
