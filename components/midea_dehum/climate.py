import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from . import midea_dehum_ns, CONF_MIDEA_DEHUM_ID

MideaDehum = midea_dehum_ns.class_("MideaDehumComponent", climate.Climate, cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MideaDehum),
    cv.Required(CONF_MIDEA_DEHUM_ID): cv.use_id(MideaDehum),
}).extend(climate.climate_schema(MideaDehum))

async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_DEHUM_ID])
    await climate.register_climate(parent, config)
