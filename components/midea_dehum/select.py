import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from . import midea_dehum_ns, CONF_MIDEA_DEHUM_ID

cg.add_define("USE_MIDEA_DEHUM_SELECT")

MideaCapabilitiesSelect = midea_dehum_ns.class_("MideaCapabilitiesSelect", select.Select, cg.Component)
MideaDehum = midea_dehum_ns.class_("MideaDehumComponent", cg.Component)

CONF_CAPABILITIES = "capabilities"

CONFIG_SCHEMA = cv.Schema({
    cv.Required(CONF_MIDEA_DEHUM_ID): cv.use_id(MideaDehum),
    cv.Optional(CONF_CAPABILITIES): select.select_schema(
        MideaCapabilitiesSelect,
        icon="mdi:chip"
    ),
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_DEHUM_ID])

    if CONF_CAPABILITIES in config:
        cg.add_define("USE_MIDEA_DEHUM_CAPABILITIES")
        s = await select.new_select(
            config[CONF_CAPABILITIES],
            options=[],
        )
        cg.add(parent.set_capabilities_select(s))
        await select.register_select(s, config[CONF_CAPABILITIES])