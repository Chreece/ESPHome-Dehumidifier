import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import CONF_ID, CONF_UART_ID
from esphome.components import uart
from esphome.core import ESPHomeError

midea_dehum_ns = cg.esphome_ns.namespace('midea_dehum')
MideaDehum = midea_dehum_ns.class_('MideaDehumComponent', cg.Component, uart.UARTDevice)

CONF_MIDEA_DEHUM_ID = CONF_ID
CONF_MIDEA_UART_ID = CONF_UART_ID

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MideaDehum),
    cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
}).extend(cv.COMPONENT_SCHEMA)

def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID], config[CONF_UART_ID])
    cg.register_component(var, config)
