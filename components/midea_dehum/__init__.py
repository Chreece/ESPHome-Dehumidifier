import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, climate, switch, binary_sensor
from esphome.const import CONF_ID, CONF_UART_ID

# Namespace matches your C++ namespace
midea_dehum_ns = cg.esphome_ns.namespace("midea_dehum")
MideaDehum = midea_dehum_ns.class_(
    "MideaDehumComponent",
    climate.Climate,
    uart.UARTDevice,
    cg.Component,
)

# Extra entities
TankFull = midea_dehum_ns.class_("TankFullBinarySensor", binary_sensor.BinarySensor)
IonSwitch = midea_dehum_ns.class_("IonSwitch", switch.Switch)
SwingSwitch = midea_dehum_ns.class_("SwingSwitch", switch.Switch)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MideaDehum),
        cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
    }
).extend(cv.COMPONENT_SCHEMA).extend(climate.CLIMATE_SCHEMA)


async def to_code(config):
    uart_comp = await cg.get_variable(config[CONF_UART_ID])
    var = cg.new_Pvariable(config[CONF_ID])  # uses default ctor
    cg.add(var.set_uart(uart_comp))          # attach UART

    # Register base component
    await cg.register_component(var, config)
    await climate.register_climate(var, config)

    # Register extra entities and attach them to C++
    tank = cg.new_Pvariable(config[CONF_ID] + "_tank")
    await binary_sensor.register_binary_sensor(tank, {"name": "Dehumidifier Tank Full"})
    cg.add(var.set_tank_full_sensor(tank))

    ion = cg.new_Pvariable(config[CONF_ID] + "_ion")
    await switch.register_switch(ion, {"name": "Dehumidifier Ion"})
    cg.add(var.set_ion_switch(ion))

    swing = cg.new_Pvariable(config[CONF_ID] + "_swing")
    await switch.register_switch(swing, {"name": "Dehumidifier Swing"})
    cg.add(var.set_swing_switch(swing))
