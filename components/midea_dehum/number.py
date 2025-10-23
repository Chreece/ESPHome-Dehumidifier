import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_ID, UNIT_HOUR, ICON_TIMER
from . import midea_dehum_ns, CONF_MIDEA_DEHUM_ID

cg.add_define("USE_MIDEA_DEHUM_NUMBER")

MideaDehum = midea_dehum_ns.class_("MideaDehumComponent", cg.Component)
MideaTimerNumber = midea_dehum_ns.class_("MideaTimerNumber", number.Number, cg.Component)

CONF_TIMER = "timer"

CONFIG_SCHEMA = cv.Schema({
    cv.Required(CONF_MIDEA_DEHUM_ID): cv.use_id(MideaDehum),
    cv.Optional(CONF_TIMER): number.number_schema(
        MideaTimerNumber,
        unit_of_measurement=UNIT_HOUR,
        icon=ICON_TIMER,,
        device_class=duration,
    ),
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_DEHUM_ID])

    if CONF_TIMER in config:
        cg.add_define("USE_MIDEA_DEHUM_TIMER")
        n = await number.new_number(
            config[CONF_TIMER],
            min_value=0,
            max_value=24.0,
            step=0.5,
        )
        cg.add(parent.set_timer_number(n))
