import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, sensor, uart
from esphome.const import CONF_ID, CONF_UART_ID, CONF_NAME

CODEOWNERS = ["@Chreece"]

midea_dehum_ns = cg.esphome_ns.namespace("midea_dehum")
MideaDehumComponent = midea_dehum_ns.class_(
    "MideaDehumComponent", climate.Climate, cg.Component, uart.UARTDevice
)

CONF_ERROR = "error"

CONFIG_SCHEMA = climate.climate_schema(MideaDehumComponent).extend(
    {
        cv.GenerateID(): cv.declare_id(MideaDehumComponent),
        cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Optional("error"): sensor.sensor_schema(),
    }
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    await uart.register_uart_device(var, config)

    if CONF_ERROR in config:
        sens = await sensor.new_sensor(config[CONF_ERROR])
        cg.add(var.set_error_sensor(sens))



