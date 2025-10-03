import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, sensor, uart, switch
from esphome.const import CONF_ID, CONF_UART_ID, CONF_NAME

CODEOWNERS = ["@Chreece"]

midea_dehum_ns = cg.esphome_ns.namespace("midea_dehum")
MideaDehumComponent = midea_dehum_ns.class_(
    "MideaDehumComponent", climate.Climate, cg.Component, uart.UARTDevice
)

CONF_ERROR = "error"
CONF_IONIZER = "ionizer"

IonizerSwitch = midea_dehum_ns.class_("IonizerSwitch", switch.Switch, cg.Component)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MideaDehum),
            cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
            cv.Optional(CONF_ERROR): sensor.sensor_schema(),
            cv.Optional(CONF_IONIZER): switch.switch_schema(IonizerSwitch),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(climate.climate_schema(MideaDehum))
)

async def to_code(config):
    uart_comp = await cg.get_variable(config[CONF_UART_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.set_uart_parent(uart_comp))

    await cg.register_component(var, config)
    await climate.register_climate(var, config)

    if CONF_ERROR in config:
        err = await sensor.new_sensor(config[CONF_ERROR])
        cg.add(var.set_error_sensor(err))

    if CONF_IONIZER in config:
        sw = await switch.new_switch(config[CONF_IONIZER])
        cg.add(var.set_ionizer_switch(sw))
