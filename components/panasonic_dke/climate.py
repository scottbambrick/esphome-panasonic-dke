# Panasonic CS-E24GKR (DKE protocol) IR climate with everything the remote has:
# 5 fan speeds + auto, swing, Powerful/Quiet (presets), louvre positions and e-ion/
# Patrol (selects). It also decodes the original remote so HA follows it.
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate_ir, select
from esphome.const import CONF_ENTITY_CATEGORY

AUTO_LOAD = ["climate_ir", "select"]
CODEOWNERS = []

CONF_VERTICAL_SWING = "vertical_swing"
CONF_HORIZONTAL_SWING = "horizontal_swing"
CONF_AIR_PURIFIER = "air_purifier"
CONF_SELF_TEST = "self_test"

ns = cg.esphome_ns.namespace("panasonic_dke")
PanasonicDKEClimate = ns.class_("PanasonicDKEClimate", climate_ir.ClimateIR)
PanasonicDKESelect = ns.class_("PanasonicDKESelect", select.Select)

VERTICAL_OPTIONS = ["Auto", "1 (top)", "2", "3", "4", "5 (bottom)"]
HORIZONTAL_OPTIONS = ["Auto", "Left", "Centre-left", "Centre", "Centre-right", "Right"]
AIR_OPTIONS = ["Off", "e-ion", "Patrol"]

CONFIG_SCHEMA = (
    climate_ir.climate_ir_with_receiver_schema(PanasonicDKEClimate)
    .extend(
        {
            cv.Optional(CONF_VERTICAL_SWING): select.select_schema(PanasonicDKESelect),
            cv.Optional(CONF_HORIZONTAL_SWING): select.select_schema(PanasonicDKESelect),
            cv.Optional(CONF_AIR_PURIFIER): select.select_schema(PanasonicDKESelect),
            cv.Optional(CONF_SELF_TEST, default=False): cv.boolean,
        }
    )
)


async def to_code(config):
    var = await climate_ir.new_climate_ir(config)
    for key, options, setter in (
        (CONF_VERTICAL_SWING, VERTICAL_OPTIONS, "set_vertical_select"),
        (CONF_HORIZONTAL_SWING, HORIZONTAL_OPTIONS, "set_horizontal_select"),
        (CONF_AIR_PURIFIER, AIR_OPTIONS, "set_air_select"),
    ):
        if key in config:
            sel = await select.new_select(config[key], options=options)
            cg.add(sel.set_parent(var))
            cg.add(getattr(var, setter)(sel))
    if config[CONF_SELF_TEST]:
        cg.add_define("PANASONIC_DKE_SELF_TEST")
