import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID, CONF_UART_ID

# Namespace
midea_dehum_ns = cg.esphome_ns.namespace("midea_dehum")
MideaDehum = midea_dehum_ns.class_("MideaDehumComponent", cg.Component, uart.UARTDevice)

CONF_MIDEA_DEHUM_ID = "midea_dehum_id"
CONF_STATUS_POLL_INTERVAL = "status_poll_interval"
CONF_HANDSHAKE = "handshake_enabled"

CONFIG_SCHEMA = (
    cv.Schema({
        cv.GenerateID(CONF_ID): cv.declare_id(MideaDehum),
        cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),

        cv.Optional(CONF_STATUS_POLL_INTERVAL, default=30000): cv.positive_int,

        cv.Optional("display_mode_setpoint", default="Setpoint"): cv.string,
        cv.Optional("display_mode_continuous", default="Continuous"): cv.string,
        cv.Optional("display_mode_smart", default="Smart"): cv.string,
        cv.Optional("display_mode_clothes_drying", default="ClothesDrying"): cv.string,
        cv.Optional(CONF_HANDSHAKE, default=True): cv.boolean,
    })
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

async def to_code(config):
    uart_comp = await cg.get_variable(config[CONF_UART_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.set_uart(uart_comp))
    await cg.register_component(var, config)

    cg.add(var.set_status_poll_interval(config[CONF_STATUS_POLL_INTERVAL]))

    cg.add(var.set_display_mode_setpoint(config["display_mode_setpoint"]))
    cg.add(var.set_display_mode_continuous(config["display_mode_continuous"]))
    cg.add(var.set_display_mode_smart(config["display_mode_smart"]))
    cg.add(var.set_display_mode_clothes_drying(config["display_mode_clothes_drying"]))
    
    if CONF_HANDSHAKE in config:
        cg.add(var.set_handshake_enabled(config[CONF_HANDSHAKE]))



