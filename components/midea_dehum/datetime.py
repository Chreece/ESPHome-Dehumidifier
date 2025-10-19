import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import datetime
from esphome.const import CONF_ID, ICON_TIMER
from . import midea_dehum_ns, CONF_MIDEA_DEHUM_ID

cg.add_define("USE_MIDEA_DEHUM_DATETIME")

MideaDehum = midea_dehum_ns.class_("MideaDehumComponent", cg.Component)

# ✅ Use datetime.datetime_ns.Datetime for newer ESPHome versions
MideaTriggerDatetime = midea_dehum_ns.class_(
    "MideaTriggerDatetime", datetime.datetime_ns.class_("Datetime"), cg.Component
)

CONF_TRIGGER_DATETIME = "trigger_datetime"

CONFIG_SCHEMA = cv.Schema({
    cv.Required(CONF_MIDEA_DEHUM_ID): cv.use_id(MideaDehum),
    cv.Optional(CONF_TRIGGER_DATETIME): datetime.datetime_schema(
        MideaTriggerDatetime,
        icon=ICON_TIMER,
    ),
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_DEHUM_ID])

    if CONF_TRIGGER_DATETIME in config:
        cg.add_define("USE_MIDEA_DEHUM_TIMER")
        dt = await datetime.new_datetime(config[CONF_TRIGGER_DATETIME])
        cg.add(parent.set_trigger_datetime(dt))