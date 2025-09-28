import esphome.codegen as cg
import esphome.config_validation as cv
<<<<<<< HEAD
from esphome.components import uart, climate
from esphome.const import CONF_ID, CONF_UART_ID

midea_dehum_ns = cg.esphome_ns.namespace("midea_dehum")
MideaDehum = midea_dehum_ns.class_("MideaDehumComponent", climate.Climate, uart.UARTDevice)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MideaDehum),
        cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
    }
).extend(climate.CLIMATE_SCHEMA).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    uart_comp = await cg.get_variable(config[CONF_UART_ID])
=======
from esphome.components import uart
from esphome.const import CONF_ID, CONF_UART_ID

# create namespace
midea_dehum_ns = cg.esphome_ns.namespace('midea_dehum')
MideaDehumComponent = midea_dehum_ns.class_(
    'MideaDehumComponent', cg.Component, uart.UARTDevice
)

CONF_UART = CONF_UART_ID

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MideaDehumComponent),
    cv.Required(CONF_UART): cv.use_id(uart.UARTComponent),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    uart_comp = await cg.get_variable(config[CONF_UART])
>>>>>>> 7f4d62cd5bb9d7e19c72e84b86286ad9604f955e
    var = cg.new_Pvariable(config[CONF_ID], uart_comp)
    await cg.register_component(var, config)
