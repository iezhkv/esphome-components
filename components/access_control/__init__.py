import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import binary_sensor, switch, web_server_base
from esphome.components import time as time_
from esphome.const import CONF_ID, CONF_MODE, CONF_TIME_ID, CONF_UID

DEPENDENCIES = ["web_server_base"]
AUTO_LOAD = ["json"]
MULTI_CONF = True

CONF_RELAY_ID = "relay_id"
CONF_DOOR_SENSOR_ID = "door_sensor_id"
CONF_RESTRICT_SENSOR_ID = "restrict_sensor_id"
CONF_DEBOUNCE_TIME = "debounce_time"
CONF_OPEN_WAIT = "open_wait"
CONF_CLOSE_WAIT = "close_wait"
CONF_BYPASS_DOOR_SENSOR = "bypass_door_sensor"
CONF_WEB_SERVER_BASE_ID = "web_server_base_id"

access_control_ns = cg.esphome_ns.namespace("access_control")
AccessControl = access_control_ns.class_("AccessControl", cg.Component)
ScanAction = access_control_ns.class_("ScanAction", automation.Action)
LockMode = access_control_ns.enum("LockMode", is_class=True)

MODES = {
    "momentary": LockMode.MOMENTARY,
    "latching": LockMode.LATCHING,
}


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(AccessControl),
        cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
        cv.Required(CONF_RELAY_ID): cv.use_id(switch.Switch),
        cv.Optional(CONF_TIME_ID): cv.use_id(time_.RealTimeClock),
        cv.Optional(CONF_DOOR_SENSOR_ID): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_RESTRICT_SENSOR_ID): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_MODE, default="momentary"): cv.enum(MODES, lower=True),
        cv.Optional(
            CONF_OPEN_WAIT, default="3s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_CLOSE_WAIT, default="10s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_BYPASS_DOOR_SENSOR, default=False): cv.boolean,
        cv.Optional(
            CONF_DEBOUNCE_TIME, default="3000ms"
        ): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # The component's id is used as the path segment in REST routes
    # (e.g. /access_control/door_controller/credentials).
    cg.add(var.set_controller_id(str(config[CONF_ID])))

    ws_base = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    cg.add(var.set_web_server_base(ws_base))

    relay = await cg.get_variable(config[CONF_RELAY_ID])
    cg.add(var.set_relay(relay))

    cg.add(var.set_mode(config[CONF_MODE]))
    cg.add(var.set_open_wait_ms(config[CONF_OPEN_WAIT].total_milliseconds))
    cg.add(var.set_close_wait_ms(config[CONF_CLOSE_WAIT].total_milliseconds))
    cg.add(var.set_bypass_door_sensor(config[CONF_BYPASS_DOOR_SENSOR]))
    cg.add(var.set_debounce_ms(config[CONF_DEBOUNCE_TIME].total_milliseconds))

    if CONF_TIME_ID in config:
        t = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(t))
    if CONF_DOOR_SENSOR_ID in config:
        ds = await cg.get_variable(config[CONF_DOOR_SENSOR_ID])
        cg.add(var.set_door_sensor(ds))
    if CONF_RESTRICT_SENSOR_ID in config:
        rs = await cg.get_variable(config[CONF_RESTRICT_SENSOR_ID])
        cg.add(var.set_restrict_sensor(rs))


@automation.register_action(
    "access_control.scan",
    ScanAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(AccessControl),
            cv.Required(CONF_UID): cv.templatable(cv.string),
        }
    ),
)
async def scan_action_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    tmpl = await cg.templatable(config[CONF_UID], args, cg.std_string)
    cg.add(var.set_uid(tmpl))
    return var
