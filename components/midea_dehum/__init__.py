import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, climate, sensor
from esphome.const import CONF_ID, CONF_UART_ID

midea_dehum_ns = cg.esphome_ns.namespace("midea_dehum")
MideaDehum = midea_dehum_ns.class_(
    "MideaDehumComponent",
    cg.Component,          # keep Component first
    climate.Climate,
    uart.UARTDevice,
)

CONF_ERROR = "error"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MideaDehum),
        cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_ERROR): sensor.sensor_schema(accuracy_decimals=0),
    }
).extend(cv.COMPONENT_SCHEMA).extend(climate.climate_schema(MideaDehum))

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    uart_comp = await cg.get_variable(config[CONF_UART_ID])
    cg.add(var.set_uart(uart_comp))

    await cg.register_component(var, config)
    await climate.register_climate(var, config)

    if CONF_ERROR in config:
        err = await sensor.new_sensor(config[CONF_ERROR])
        cg.add(var.set_error_sensor(err))
