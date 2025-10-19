import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    CONF_ID,
    UNIT_HOURS,
    ICON_TIMER_OUTLINE,
)
from . import midea_dehum_ns, MideaDehumComponent, CONF_MIDEA_DEHUM_ID

DEPENDENCIES = ["midea_dehum"]

MideaTimerNumber = midea_dehum_ns.class_("MideaTimerNumber", number.Number)

CONFIG_SCHEMA = number.NUMBER_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(MideaTimerNumber),
        cv.GenerateID(CONF_MIDEA_DEHUM_ID): cv.use_id(MideaDehumComponent),
    }
)

async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_DEHUM_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await number.register_number(
        var, config,
        min_value=0, max_value=24, step=0.5,
        unit_of_measurement=UNIT_HOURS,
        icon=ICON_TIMER_OUTLINE
    )
    cg.add(var.set_parent(parent))
    cg.add(parent.set_timer_number(var))