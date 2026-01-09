import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from . import midea_dehum_ns, CONF_MIDEA_DEHUM_ID

MideaDehum = midea_dehum_ns.class_("MideaDehumComponent", climate.Climate, cg.Component)

CONF_SWING = "swing"
CONF_HORIZONTAL_SWING = "horizontal_swing"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MideaDehum),
    cv.Required(CONF_MIDEA_DEHUM_ID): cv.use_id(MideaDehum),
    cv.Optional(CONF_SWING, default=False): cv.boolean,
    cv.Optional(CONF_HORIZONTAL_SWING, default=False): cv.boolean,
}).extend(climate.climate_schema(MideaDehum))

async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_DEHUM_ID])
    await climate.register_climate(parent, config)
    if config[CONF_SWING]:
        cg.add_define("USE_MIDEA_DEHUM_SWING")

    if config[CONF_HORIZONTAL_SWING]:
        cg.add_define("USE_MIDEA_DEHUM_HORIZONTAL_SWING")

