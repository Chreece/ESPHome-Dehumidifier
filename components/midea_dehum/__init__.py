import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, climate, switch, binary_sensor
from esphome.const import CONF_ID, CONF_UART_ID

# Namespace must match C++ side
midea_dehum_ns = cg.esphome_ns.namespace("midea_dehum")
MideaDehum = midea_dehum_ns.class_(
    "MideaDehumComponent",
    climate.Climate,
    uart.UARTDevice,
    cg.Component,
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MideaDehum),
        cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
    }
).extend(cv.COMPONENT_SCHEMA).extend(climate.CLIMATE_SCHEMA)


async def to_code(config):
    uart_comp = await cg.get_variable(config[CONF_UART_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.set_uart(uart_comp))

    # Register main component + climate entity
    await cg.register_component(var, config)
    await climate.register_climate(var, config)

    # Tank full binary_sensor
    tank = await binary_sensor.new_binary_sensor({"name": "Dehumidifier Tank Full"})
    cg.add(var.set_tank_full_sensor(tank))

    # Ion switch
    ion = await switch.new_switch({"name": "Dehumidifier Ion"})
    cg.add(var.set_ion_switch(ion))
