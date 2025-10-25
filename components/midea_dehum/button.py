import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import ENTITY_CATEGORY_CONFIG
from . import midea_dehum_ns, CONF_MIDEA_DEHUM_ID

cg.add_define("USE_MIDEA_DEHUM_BUTTON")

MideaDehum = midea_dehum_ns.class_("MideaDehumComponent", cg.Component)

CONF_FILTER_CLEANED = "filter_cleaned"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MideaDehum),
    cv.Required(CONF_MIDEA_DEHUM_ID): cv.use_id(MideaDehum),
    cv.Optional(CONF_FILTER_CLEANED): button.button_schema(
        MideaDehum,
        icon="mdi:broom",
        entity_category=ENTITY_CATEGORY_CONFIG,
    ),
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_DEHUM_ID])
    
    if CONF_FILTER_CLEANED in config:
      cg.add_define("USE_MIDEA_DEHUM_FILTER_BUTTON")
      btn = await button.new_button(config[CONF_FILTER_CLEANED])
      cg.add(parent.set_filter_cleaned_button(btn))
