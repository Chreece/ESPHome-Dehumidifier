import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, climate
from esphome.const import CONF_ID, CONF_UART_ID

raise Exception(">>> DEBUG: midea_dehum.py imported")
# Namespace must match the C++ side (namespace midea_dehum)
midea_dehum_ns = cg.esphome_ns.namespace("midea_dehum")
MideaDehum = midea_dehum_ns.class_(
    "MideaDehumComponent", climate.Climate, uart.UARTDevice
)

# Schema for YAML
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MideaDehum),
        cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
    }
).extend(cv.COMPONENT_SCHEMA).extend(climate.CLIMATE_SCHEMA)


async def to_code(config):
    # Debug print, you should see this in logs if the file loads
    print(">>> [midea_dehum] CONFIG_SCHEMA loaded OK")

    uart_comp = await cg.get_variable(config[CONF_UART_ID])
    var = cg.new_Pvariable(config[CONF_ID], uart_comp)

    # Register the base component
    await cg.register_component(var, config)

    # Register it as a Climate entity
    await climate.register_climate(var, config)
