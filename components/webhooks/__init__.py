import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components.web_server_base import (
    CONF_WEB_SERVER_BASE_ID,
    WebServerBase,
)
from esphome.const import CONF_ID, CONF_METHOD
from esphome.core import Lambda

CODEOWNERS = ["@iezhkv"]
DEPENDENCIES = ["web_server"]
MULTI_CONF = True

CONF_PATH = "path"
CONF_ON_REQUEST = "on_request"
CONF_RESPONSE = "response"
CONF_STATUS = "status"
CONF_CONTENT_TYPE = "content_type"
CONF_BODY = "body"
CONF_JSON = "json"

webhooks_ns = cg.esphome_ns.namespace("webhooks")
Webhook = webhooks_ns.class_("Webhook", cg.Component)

AsyncWebServerRequest = cg.global_ns.class_("AsyncWebServerRequest")
AsyncWebServerRequestPtr = AsyncWebServerRequest.operator("ptr")

# Map YAML strings -> ESP-IDF httpd method constants.
METHODS = {
    "GET": cg.global_ns.HTTP_GET,
    "POST": cg.global_ns.HTTP_POST,
    "PUT": cg.global_ns.HTTP_PUT,
    "DELETE": cg.global_ns.HTTP_DELETE,
    "PATCH": cg.global_ns.HTTP_PATCH,
}

RESPONSE_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_STATUS, default=200): cv.int_range(min=100, max=599),
        cv.Optional(CONF_CONTENT_TYPE): cv.string,
        cv.Exclusive(CONF_BODY, "body"): cv.templatable(cv.string),
        cv.Exclusive(CONF_JSON, "body"): cv.Any(
            cv.lambda_,
            cv.All(cv.Schema({cv.string: cv.templatable(cv.string_strict)})),
        ),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Webhook),
        cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(WebServerBase),
        cv.Required(CONF_PATH): cv.string,
        cv.Optional(CONF_METHOD, default="GET"): cv.one_of(*METHODS, upper=True),
        cv.Optional(CONF_ON_REQUEST): automation.validate_automation({}),
        cv.Optional(CONF_RESPONSE): RESPONSE_SCHEMA,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    base = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    var = cg.new_Pvariable(config[CONF_ID], base)
    await cg.register_component(var, config)

    cg.add(var.set_path(config[CONF_PATH]))
    cg.add(var.set_method(METHODS[config[CONF_METHOD]]))

    if CONF_RESPONSE in config:
        resp = config[CONF_RESPONSE]
        cg.add(var.set_response_status(resp[CONF_STATUS]))
        if CONF_CONTENT_TYPE in resp:
            cg.add(var.set_response_content_type(resp[CONF_CONTENT_TYPE]))

        if CONF_BODY in resp:
            template_ = await cg.templatable(
                resp[CONF_BODY],
                [(AsyncWebServerRequestPtr, "request")],
                cg.std_string,
            )
            cg.add(var.set_response_body(template_))

        if CONF_JSON in resp:
            json_ = resp[CONF_JSON]
            if isinstance(json_, Lambda):
                args = [(AsyncWebServerRequestPtr, "request"), (cg.JsonObject, "root")]
                lambda_ = await cg.process_lambda(json_, args, return_type=cg.void)
                cg.add(var.set_response_json_lambda(lambda_))
            else:
                cg.add(var.init_response_json_object(len(json_)))
                for key, value in json_.items():
                    template_ = await cg.templatable(
                        value,
                        [(AsyncWebServerRequestPtr, "request")],
                        cg.std_string,
                    )
                    cg.add(var.add_response_json_field(key, template_))

    for conf in config.get(CONF_ON_REQUEST, []):
        await automation.build_callback_automation(
            var,
            "add_on_request_callback",
            [(AsyncWebServerRequestPtr, "request")],
            conf,
        )
