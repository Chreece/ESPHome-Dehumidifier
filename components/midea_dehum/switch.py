import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from . import midea_dehum_ns, CONF_MIDEA_DEHUM_ID

MideaDehum = midea_dehum_ns.class_("MideaDehumComponent", cg.Component)

CONF_IONIZER = "ionizer"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(switch.Switch),
    cv.Required(CONF_MIDEA_DEHUM_ID): cv.use_id(MideaDehum),
    cv.Required(CONF_IONIZER): switch.switch_schema(icon="mdi:air-purifier")
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_MIDEA_DEHUM_ID])
    sw = await switch.new_switch(config[CONF_IONIZER])
    cg.add(parent.set_ion_switch(sw))
