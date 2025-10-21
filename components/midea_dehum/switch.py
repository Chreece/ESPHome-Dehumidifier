import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from . import midea_dehum_ns, CONF_MIDEA_DEHUM_ID

cg.add_define("USE_MIDEA_DEHUM_SWITCH")

MideaIonSwitch = midea_dehum_ns.class_("MideaIonSwitch", switch.Switch, cg.Component)
MideaSwingSwitch = midea_dehum_ns.class_("MideaSwingSwitch", switch.Switch, cg.Component)
MideaBeepSwitch = midea_dehum_ns.class_("MideaBeepSwitch", switch.Switch, cg.Component)
MideaSleepSwitch = midea_dehum_ns.class_("MideaSleepSwitch", switch.Switch, cg.Component)
MideaPumpSwitch = midea_dehum_ns.class_("MideaPumpSwitch", switch.Switch, cg.Component)
MideaDehum = midea_dehum_ns.class_("MideaDehumComponent", cg.Component)

CONF_IONIZER = "ionizer"
CONF_SWING = "swing"
CONF_BEEP = "beep"
CONF_SLEEP = "sleep"
CONF_PUMP = "pump"

CONFIG_SCHEMA = cv.Schema({
    cv.Required(CONF_MIDEA_DEHUM_ID): cv.use_id(MideaDehum),
    cv.Optional(CONF_IONIZER): switch.switch_schema(MideaIonSwitch, icon="mdi:air-purifier"),
    cv.Optional(CONF_SWING): switch.switch_schema(MideaSwingSwitch, icon="mdi:arrow-oscillating"),
    cv.Optional(CONF_BEEP): switch.switch_schema(MideaBeepSwitch, icon="mdi:volume-high"),
    cv.Optional(CONF_SLEEP): switch.switch_schema(MideaSleepSwitch, icon="mdi:sleep"),
    cv.Optional(CONF_PUMP): switch.switch_schema(MideaPumpSwitch, icon="mdi:water-pump"),
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_DEHUM_ID])

    if CONF_IONIZER in config:
        cg.add_define("USE_MIDEA_DEHUM_ION")
        sw = await switch.new_switch(config[CONF_IONIZER])
        cg.add(parent.set_ion_switch(sw))

    if CONF_SWING in config:
        cg.add_define("USE_MIDEA_DEHUM_SWING")
        sw = await switch.new_switch(config[CONF_SWING])
        cg.add(parent.set_swing_switch(sw))

    if CONF_PUMP in config:
        cg.add_define("USE_MIDEA_DEHUM_PUMP")
        sw = await switch.new_switch(config[CONF_PUMP])
        cg.add(parent.set_pump_switch(sw))

    if CONF_BEEP in config:
        cg.add_define("USE_MIDEA_DEHUM_BEEP")
        sw = await switch.new_switch(config[CONF_BEEP])
        cg.add(parent.set_beep_switch(sw))

    if CONF_SLEEP in config:
        cg.add_define("USE_MIDEA_DEHUM_SLEEP")
        sw = await switch.new_switch(config[CONF_SLEEP])
        cg.add(parent.set_sleep_switch(sw))
