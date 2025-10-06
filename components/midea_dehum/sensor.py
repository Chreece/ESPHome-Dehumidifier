import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import UNIT_EMPTY
from . import midea_dehum_ns, CONF_MIDEA_DEHUM_ID

MideaDehum = midea_dehum_ns.class_("MideaDehumComponent", cg.Component)

CONF_ERROR = "error"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(sensor.Sensor),
    cv.Required(CONF_MIDEA_DEHUM_ID): cv.use_id(MideaDehum),
    cv.Required(CONF_ERROR): sensor.sensor_schema(),
}).extend({
    CONF_ERROR: {
        cv.Optional("unit_of_measurement", default=UNIT_EMPTY): cv.string_strict,
        cv.Optional("icon", default="mdi:alert-outline"): cv.icon,
        cv.Optional("accuracy_decimals", default=0): cv.int_,
    }
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_DEHUM_ID])
    sens = await sensor.new_sensor(config[CONF_ERROR])
    cg.add(parent.set_error_sensor(sens))
