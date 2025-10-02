import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, climate, switch, binary_sensor
from esphome.const import CONF_ID, CONF_UART_ID

midea_dehum_ns = cg.esphome_ns.namespace("midea_dehum")
MideaDehum = midea_dehum_ns.class_(
    "MideaDehumComponent",
    climate.Climate,
    uart.UARTDevice,
    cg.Component,
)
IonSwitch = midea_dehum_ns.class_("IonSwitch", switch.Switch)

CONF_TANK = "tank_full"
CONF_ION = "ion"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MideaDehum),
        cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_TANK): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_ION): switch.switch_schema(IonSwitch),
    }
).extend(cv.COMPONENT_SCHEMA).extend(climate.climate_schema(MideaDehum))


async def to_code(config):
    uart_comp = await cg.get_variable(config[CONF_UART_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.set_uart(uart_comp))

    await cg.register_component(var, config)
    await climate.register_climate(var, config)

    # Tank binary_sensor
    if CONF_TANK in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_TANK])
        cg.add(var.set_tank_full_sensor(sens))

    # Ion switch
    if CONF_ION in config:
        sw = await switch.new_switch(config[CONF_ION])
        cg.add(var.set_ion_switch(sw))
