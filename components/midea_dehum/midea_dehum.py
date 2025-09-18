import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import CONF_ID, CONF_UART_ID
from esphome.components import uart
from esphome import core as _core

# Create namespace matching your C++ class name
midea_dehum_ns = cg.esphome_ns.namespace('midea_dehum')
# C++ class name must match exactly: MideaDehumComponent
MideaDehum = midea_dehum_ns.class_('MideaDehumComponent', cg.Component, uart.UARTDevice)

# YAML keys
CONF_UART = CONF_UART_ID

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MideaDehum),
    cv.Required(CONF_UART): cv.use_id(uart.UARTComponent),
}).extend(cv.COMPONENT_SCHEMA)


def to_code(config):
    # create the C++ object with the UARTComponent pointer
    var = cg.new_Pvariable(config[CONF_ID], config[CONF_UART])
    cg.register_component(var, config)
