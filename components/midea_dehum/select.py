import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_ID

from . import midea_dehum_ns, CONF_MIDEA_DEHUM_ID

cg.add_define("USE_MIDEA_DEHUM_SELECT")

MideaLightSelect = midea_dehum_ns.class_("MideaLightSelect", select.Select, cg.Component)
MideaDehum = midea_dehum_ns.class_("MideaDehumComponent", cg.Component)

CONF_LIGHT = "light"

CONFIG_SCHEMA = cv.Schema({
    cv.Required(CONF_MIDEA_DEHUM_ID): cv.use_id(MideaDehum),
    cv.Optional(CONF_LIGHT): select.select_schema(
        MideaLightSelect,
        icon="mdi:lightbulb",
        options=["Auto", "Off", "Low", "High"]
    ),
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_DEHUM_ID])

    if CONF_LIGHT in config:
        cg.add_define("USE_MIDEA_DEHUM_LIGHT")
        sel = await select.new_select(config[CONF_LIGHT])
        cg.add(parent.set_light_select(sel))
