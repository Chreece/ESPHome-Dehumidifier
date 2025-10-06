import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import UNIT_EMPTY, ICON_ALERT
from . import midea_dehum_ns, CONF_MIDEA_DEHUM_ID

# Reference to the parent component
MideaDehum = midea_dehum_ns.class_("MideaDehumComponent", cg.Component)

CONF_ERROR = "error"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(sensor.Sensor),
    cv.Required(CONF_MIDEA_DEHUM_ID): cv.use_id(MideaDehum),
    cv.Required(CONF_ERROR): sensor.sensor_schema(UNIT_EMPTY, ICON_ALERT, 0),
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_DEHUM_ID])
    sens = await sensor.new_sensor(config[CONF_ERROR])
    cg.add(parent.set_error_sensor(sens))
