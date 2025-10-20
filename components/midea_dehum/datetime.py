import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import datetime
from . import midea_dehum_ns, CONF_MIDEA_DEHUM_ID

CONF_TRIGGER_DATETIME = "trigger_datetime"

MideaTriggerDatetime = midea_dehum_ns.class_("MideaTriggerDatetime", datetime.DateTimeEntity,  cg.Component)
MideaDehum = midea_dehum_ns.class_("MideaDehumComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.Required(CONF_MIDEA_DEHUM_ID): cv.use_id(MideaDehum),
    cv.Optional(CONF_TRIGGER_DATETIME): datetime.DATETIME_SCHEMA.extends({
    cv.GenerateID(): cv.declare_id(MideaTriggerDatetime),})
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_DEHUM_ID])
    if CONF_TRIGGER_DATETIME in config:
        cg.add_define("USE_MIDEA_DEHUM_DATETIME")
        dt_ent = await datetime.new_datetime(config[CONF_TRIGGER_DATETIME])
        await cg.register_component(dt_ent, config[CONF_TRIGGER_DATETIME])
        await datetime.register_datetime(dt_ent, config[CONF_TRIGGER_DATETIME])
        cg.add(parent.set_trigger_datetime(dt_ent))
