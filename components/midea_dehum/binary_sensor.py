import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import DEVICE_CLASS_PROBLEM
from . import midea_dehum_ns, CONF_MIDEA_DEHUM_ID

cg.add_define("USE_MIDEA_DEHUM_BINARY_SENSOR")

MideaDehum = midea_dehum_ns.class_("MideaDehumComponent", cg.Component)

CONF_BUCKET_FULL = "bucket_full"
CONF_FILTER_REQUEST = "filter_request"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MideaDehum),
    cv.Required(CONF_MIDEA_DEHUM_ID): cv.use_id(MideaDehum),
    cv.Optional(CONF_BUCKET_FULL): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_PROBLEM,
        icon="mdi:cup-water",
    ),
    cv.Optional(CONF_FILTER_REQUEST): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_PROBLEM,
        icon="mdi:air-filter",
    ),
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_DEHUM_ID])

    if CONF_BUCKET_FULL in config:
        cg.add_define("USE_MIDEA_DEHUM_BUCKET")
        bsens = await binary_sensor.new_binary_sensor(config[CONF_BUCKET_FULL])
        cg.add(parent.set_bucket_full_sensor(bsens))

    if CONF_FILTER_REQUEST in config:
        cg.add_define("USE_MIDEA_DEHUM_FILTER")
        bsens = await binary_sensor.new_binary_sensor(config[CONF_FILTER_REQUEST])
        cg.add(parent.set_filter_request_sensor(bsens))