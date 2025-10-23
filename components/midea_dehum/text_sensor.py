import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC
from . import midea_dehum_ns, CONF_MIDEA_DEHUM_ID

cg.add_define("USE_MIDEA_DEHUM_TEXT")

MideaDehum = midea_dehum_ns.class_("MideaDehumComponent", cg.Component)
MideaCapabilitiesTextSensor = midea_dehum_ns.class_(
    "MideaCapabilitiesTextSensor", text_sensor.TextSensor, cg.Component
)

CONF_CAPABILITIES = "capabilities"

CONFIG_SCHEMA = cv.Schema({
    cv.Required(CONF_MIDEA_DEHUM_ID): cv.use_id(MideaDehum),
    cv.Optional(CONF_CAPABILITIES): text_sensor.text_sensor_schema(
        icon="mdi:chip",
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_DEHUM_ID])

    if CONF_CAPABILITIES in config:
        cg.add_define("USE_MIDEA_DEHUM_CAPABILITIES")
        sens = await text_sensor.new_text_sensor(config[CONF_CAPABILITIES])
        cg.add(parent.set_capabilities_text_sensor(sens))
