import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import UNIT_EMPTY, UNIT_PERCENT, DEVICE_CLASS_PM25
from . import midea_dehum_ns, CONF_MIDEA_DEHUM_ID

cg.add_define("USE_MIDEA_DEHUM_SENSOR")

MideaDehum = midea_dehum_ns.class_("MideaDehumComponent", cg.Component)

CONF_ERROR = "error"
CONF_TANK_LEVEL = 'tank_level'
CONF_PM25 = 'pm25'

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MideaDehum),
    cv.Required(CONF_MIDEA_DEHUM_ID): cv.use_id(MideaDehum),
    cv.Optional(CONF_ERROR): sensor.sensor_schema(
        unit_of_measurement=UNIT_EMPTY,
        icon="mdi:alert-outline",
        accuracy_decimals=0,
    ),
    cv.Optional(CONF_TANK_LEVEL): sensor.sensor_schema(
        unit_of_measurement=UNIT_PERCENT,
        icon="mdi:cup",
        accuracy_decimals=0,
    ),
    cv.Optional(CONF_PM25): sensor.sensor_schema(
        device_class=DEVICE_CLASS_PM25,
    ),
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_DEHUM_ID])

    if CONF_ERROR in config:
        cg.add_define("USE_MIDEA_DEHUM_ERROR")
        sens = await sensor.new_sensor(config[CONF_ERROR])
        cg.add(parent.set_error_sensor(sens))

    if CONF_TANK_LEVEL in config:
        cg.add_define("USE_MIDEA_DEHUM_TANK_LEVEL")
        tank = await sensor.new_sensor(config[CONF_TANK_LEVEL])
        cg.add(parent.set_tank_level_sensor(tank))

    if CONF_PM25 in config:
        cg.add_define("USE_MIDEA_DEHUM_PM25")
        pm25 = await sensor.new_sensor(config[CONF_PM25])
        cg.add(parent.set_pm25_sensor(pm25))