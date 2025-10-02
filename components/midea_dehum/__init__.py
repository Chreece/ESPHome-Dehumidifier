import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

DEPENDENCIES = ["uart", "climate"]

midea_dehum_ns = cg.esphome_ns.namespace('midea_dehum')
MideaDehumComponent = midea_dehum_ns.class_('MideaDehumComponent', cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MideaDehumComponent),
    cv.Required('uart_id'): cv.use_id(uart.UARTComponent),
})

def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    uart_component = yield cg.get_variable(config['uart_id'])
    cg.add(var.set_uart(uart_component))

