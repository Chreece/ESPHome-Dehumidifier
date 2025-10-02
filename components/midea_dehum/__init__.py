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

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MideaDehum),
        cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
    }
).extend(cv.COMPONENT_SCHEMA).extend(climate.climate_schema(MideaDehum))


async def to_code(config):
    uart_comp = await cg.get_variable(config[CONF_UART_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.set_uart(uart_comp))

    # Register main component + climate
    await cg.register_component(var, config)
    await climate.register_climate(var, config)

    # Tank full binary_sensor
    tank = cg.new_Pvariable(binary_sensor.BinarySensor.new())
    await binary_sensor.register_binary_sensor(tank, {"name": "Dehumidifier Tank Full"})
    cg.add(var.set_tank_full_sensor(tank))

    # Ion switch
    ion = cg.new_Pvariable(switch.Switch.new())
    await switch.register_switch(ion, {"name": "Dehumidifier Ion"})
    cg.add(var.set_ion_switch(ion))
