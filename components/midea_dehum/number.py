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
    cv.Optional(CONF_TIMER): number.NUMBER_SCHEMA.extend({
        cv.GenerateID(): cv.declare_id(MideaTimerNumber),
    }),
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_DEHUM_ID])

    if CONF_TIMER in config:
        cg.add_define("USE_MIDEA_DEHUM_TIMER")
        conf = config[CONF_TIMER]

        n = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(n, conf)
        await number.register_number(
            n,
            conf,
            min_value=0.5,
            max_value=24.0,
            step=0.5,
            unit_of_measurement=UNIT_HOUR,
            icon=ICON_TIMER,
        )
        cg.add(parent.set_timer_number(n))