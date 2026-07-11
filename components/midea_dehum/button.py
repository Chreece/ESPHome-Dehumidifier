import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import ENTITY_CATEGORY_CONFIG
from . import midea_dehum_ns, CONF_MIDEA_DEHUM_ID

cg.add_define("USE_MIDEA_DEHUM_BUTTON")

MideaFilterCleanedButton = midea_dehum_ns.class_("MideaFilterCleanedButton", button.Button, cg.Component)
MideaResetWaterLevelButton = midea_dehum_ns.class_("MideaResetWaterLevelButton", button.Button, cg.Component)
MideaDehum = midea_dehum_ns.class_("MideaDehumComponent", cg.Component)

CONF_FILTER_CLEANED = "filter_cleaned"
CONF_RESET_WATER_LEVEL = "reset_water_level"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MideaDehum),
    cv.Required(CONF_MIDEA_DEHUM_ID): cv.use_id(MideaDehum),
    cv.Optional(CONF_FILTER_CLEANED): button.button_schema(
        MideaFilterCleanedButton,
        icon="mdi:broom",
        entity_category=ENTITY_CATEGORY_CONFIG,
    ),
    cv.Optional(CONF_RESET_WATER_LEVEL): button.button_schema(
        MideaResetWaterLevelButton,
        icon="mdi:water-sync",
        entity_category=ENTITY_CATEGORY_CONFIG,
    ),
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_DEHUM_ID])

    if CONF_FILTER_CLEANED in config:
        cg.add_define("USE_MIDEA_DEHUM_FILTER_BUTTON")
        btn = await button.new_button(config[CONF_FILTER_CLEANED])
        cg.add(parent.set_filter_cleaned_button(btn))

    if CONF_RESET_WATER_LEVEL in config:
        cg.add_define("USE_MIDEA_DEHUM_RESET_WATER_LEVEL")
        btn = await button.new_button(config[CONF_RESET_WATER_LEVEL])
        cg.add(parent.set_reset_water_level_button(btn))
