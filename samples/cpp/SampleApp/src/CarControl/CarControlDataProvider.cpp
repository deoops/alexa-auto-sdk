/*
 * Copyright 2019-2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *     http://aws.amazon.com/apache2.0/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include <sstream>
#include <fstream>

#include <SampleApp/CarControl/CarControlDataProvider.h>
#include <AACE/CarControl/CarControlAssets.h>

using namespace aace::carControl::config;
using namespace aace::carControl::assets;

namespace sampleApp {
namespace carControl {

// Initialize static data structures
std::unordered_map<std::string, BoolController> CarControlDataProvider::m_boolControllers;
std::unordered_map<std::string, ModeController> CarControlDataProvider::m_modeControllers;
std::unordered_map<std::string, RangeController> CarControlDataProvider::m_rangeControllers;

BoolController& CarControlDataProvider::getBoolController(
    const std::string& controlId,
    const std::string& controllerId) {
    return m_boolControllers[genKey(controlId, controllerId)];
}

ModeController& CarControlDataProvider::getModeController(
    const std::string& controlId,
    const std::string& controllerId) {
    return m_modeControllers[genKey(controlId, controllerId)];
}

RangeController& CarControlDataProvider::getRangeController(
    const std::string& controlId,
    const std::string& controllerId) {
    return m_rangeControllers[genKey(controlId, controllerId)];
}

void CarControlDataProvider::initialize(std::vector<std::shared_ptr<aace::core::config::EngineConfiguration>> configs) {
    json carControl;

    // See if a car control configuration is specified that will overrride
    // the generated one
    for (auto config : configs) {
        auto configStream = config->getStream();
        try {
            json j = json::parse(*configStream);

            if (j.find("aace.carControl") != j.end()) {
                carControl = j["aace.carControl"];
            }
        } catch (json::exception& e) {
        }

        // Always reset the config otherwise the Auto SDK Engine will not find it
        configStream->clear();
        configStream->seekg(0);

        // If found break out of loop
        if (!carControl.empty()) {
            break;
        }
    }

    // Build client configuration from configuration file
    try {
        if (carControl.empty()) {
            return;
        }

        json endpoints = carControl["endpoints"];

        for (auto& endpoint : endpoints) {
            if (!endpoint.empty()) {
                std::string endpointId = endpoint["endpointId"];
                json capabilities = endpoint["capabilities"];
                for (auto& capability : capabilities) {
                    if (!capability.empty()) {
                        std::string controller = capability["interface"];
                        if (controller == "Alexa.ModeController") {
                            std::string controllerId = capability["instance"];
                            json configuration = capability["configuration"];
                            if (!configuration.empty()) {
                                json supportedModes = configuration["supportedModes"];
                                if (!supportedModes.empty()) {
                                    ModeController modeController;
                                    for (auto& mode : supportedModes) {
                                        modeController.addMode(mode["value"]);
                                    }
                                    m_modeControllers[genKey(endpointId, controllerId)] = modeController;
                                }
                            }
                        } else if (controller == "Alexa.RangeController") {
                            std::string controllerId = capability["instance"];
                            json configuration = capability["configuration"];
                            if (!configuration.empty()) {
                                json supportedRange = configuration["supportedRange"];
                                if (!supportedRange.empty()) {
                                    double min = supportedRange["minimumValue"];
                                    double max = supportedRange["maximumValue"];
                                    m_rangeControllers[genKey(endpointId, controllerId)] = RangeController(min, max);
                                }
                            }
                        } else if (controller == "Alexa.ToggleController") {
                            std::string controllerId = capability["instance"];
                            m_boolControllers[genKey(endpointId, controllerId)] = BoolController();
                        } else if (controller == "Alexa.PowerController") {
                            m_boolControllers[genKey(endpointId)] = BoolController();
                        }
                    }
                }
            }
        }
    } catch (json::exception& e) {
    }
}

std::string CarControlDataProvider::genKey(std::string endpointId, std::string controllerId) {
    return endpointId + "#" + controllerId;
}

// Build an example car control configuration for the Auto SDK Engine
// See modules/car-control/assets/assets-1P.json for all the friendly names for each asset
std::shared_ptr<aace::core::config::EngineConfiguration> CarControlDataProvider::generateCarControlConfig() {
    auto config = CarControlConfiguration::create();

    // clang-format off

    //--------------------------------------------------------------------------
    // Important note: See the car control module README at
    // modules/car-control/README.md for the suggested modeling of endpoints.
    // The sample configuration in the README document is the source of truth
    // for configuring features supported for car control rather than what is
    // shown below.
    //--------------------------------------------------------------------------
    config
        //---------------------------------------------------------------------
        // Define the zones of the vehicle.
        // Ensure the endpoint IDs added to the zones are also defined.
        //---------------------------------------------------------------------
        ->createZone("zone.all")
            .addAssetId(alexa::location::ALL)
            .addMembers({
                "all.fan",
                "all.heater",
                "ac",
                "vent",
                "ambient.light",
                "reading.light"})
        .createZone("zone.rear")
            .addAssetId(alexa::location::REAR)
            .addMembers({"rear.windshield"})
        .createZone("zone.front")
            .addAssetId(alexa::location::FRONT)
            .addMembers({
                "front.light",
                "driver.seat",
                "passenger.seat"})
        .createZone("zone.driver")
            .addAssetId(alexa::location::DRIVER)
            .addAssetId(alexa::location::LEFT)
            .addMembers({
                "driver.fan",
                "driver.heater",
                "driver.seat",
                "driver.light",
                "driver.window"})
        .createZone("zone.passenger")
            .addAssetId(alexa::location::PASSENGER)
            .addAssetId(alexa::location::RIGHT)
            .addMembers({
                "passenger.fan",
                "passenger.heater",
                "passenger.seat",
                "passenger.light"})
        .createZone("zone.secondRow")
            .addAssetId(alexa::location::SECOND_ROW)
            .addMembers({"secondRow.heater", "secondRow.light"})

        // Since "zone.all" is set to default, utterances matching endpoints in 
        // this zone take precedence
        .setDefaultZone("zone.all")

        //---------------------------------------------------------------------
        // Create "fan" endpoints for various zones.
        //
        // Things to try:
        //    "Alexa, turn [on|off] the fan"
        //    "Alexa, turn [on|off] the [driver|passenger] fan"
        //    "Alexa, set the fan speed to [low|minimum|medium|high|max]"
        //    "Alexa, set the fan to <value between 1 and 10>"
        //    "Alexa, turn up the blower"
        //    "Alexa, [increase|decrease] the fan speed by 3"
        //---------------------------------------------------------------------  
        .createEndpoint("all.fan")
            .addAssetId(alexa::device::FAN)
            .addPowerController(false)
            .addRangeController("speed", false, 1, 10, 1)
                .addAssetId(alexa::setting::FAN_SPEED)
                .addPreset(1)
                    .addAssetId(alexa::value::LOW)
                    .addAssetId(alexa::value::MINIMUM)
                .addPreset(5)
                    .addAssetId(alexa::value::MEDIUM)
                .addPreset(10)
                    .addAssetId(alexa::value::HIGH)
                    .addAssetId(alexa::value::MAXIMUM)
        .createEndpoint("driver.fan")
            .addAssetId(alexa::device::FAN)
            .addPowerController(false)
            .addRangeController("speed", false, 1, 10, 1)
                .addAssetId(alexa::setting::FAN_SPEED)
                .addPreset(1)
                    .addAssetId(alexa::value::LOW)
                    .addAssetId(alexa::value::MINIMUM)
                .addPreset(5)
                    .addAssetId(alexa::value::MEDIUM)
                .addPreset(10)
                    .addAssetId(alexa::value::HIGH)
                    .addAssetId(alexa::value::MAXIMUM)
        .createEndpoint("passenger.fan")
            .addAssetId(alexa::device::FAN)
            .addPowerController(false)
            .addRangeController("speed", false, 1, 10, 1)
                .addAssetId(alexa::setting::FAN_SPEED)
                .addPreset(1)
                    .addAssetId(alexa::value::LOW)
                    .addAssetId(alexa::value::MINIMUM)
                .addPreset(5)
                    .addAssetId(alexa::value::MEDIUM)
                .addPreset(10)
                    .addAssetId(alexa::value::HIGH)
                    .addAssetId(alexa::value::MAXIMUM)
        //---------------------------------------------------------------------
        // Create a "heater" endpoint for various zones. 
        //
        // Things to try:
        //    "Alexa, turn [on|off] the heater"
        //    "Alexa, turn [on|off] the [driver|passenger|second row] heater"
        //    "Alexa, set the temperature to [low|minimum|medium|high|max]"
        //    "Alexa, set the temperature to <value between 60 and 90>"
        //    "Alexa, [increase/decrease] the temperature"
        //    "Alexa, increase the temperature by 4"
        //---------------------------------------------------------------------
        .createEndpoint("all.heater")
            .addAssetId(alexa::device::HEATER)
            .addAssetId(alexa::device::COOLER)
            .addPowerController(false)
            .addRangeController(
                "temperature", false, 60, 90, 1, alexa::unit::FAHRENHEIT)
                .addAssetId(alexa::setting::TEMPERATURE)
                .addAssetId(alexa::setting::HEAT)
                .addPreset(60)
                    .addAssetId(alexa::value::LOW)
                    .addAssetId(alexa::value::MINIMUM)
                .addPreset(75)
                    .addAssetId(alexa::value::MEDIUM)
                .addPreset(90)
                    .addAssetId(alexa::value::HIGH)
                    .addAssetId(alexa::value::MAXIMUM)
        .createEndpoint("driver.heater")
            .addAssetId(alexa::device::HEATER)
            .addAssetId(alexa::device::COOLER)
            .addPowerController(false)
            .addRangeController("temperature", false, 60, 90, 1)
                .addAssetId(alexa::setting::TEMPERATURE)
                .addAssetId(alexa::setting::HEAT)
                .addPreset(60)
                    .addAssetId(alexa::value::LOW)
                    .addAssetId(alexa::value::MINIMUM)
                .addPreset(75)
                    .addAssetId(alexa::value::MEDIUM)
                .addPreset(90)
                    .addAssetId(alexa::value::HIGH)
                    .addAssetId(alexa::value::MAXIMUM)
        .createEndpoint("passenger.heater")
            .addAssetId(alexa::device::HEATER)
            .addAssetId(alexa::device::COOLER)
            .addPowerController(false)
            .addRangeController("temperature", false, 60, 90, 1)
                .addAssetId(alexa::setting::TEMPERATURE)
                .addAssetId(alexa::setting::HEAT)
                .addPreset(60)
                    .addAssetId(alexa::value::LOW)
                    .addAssetId(alexa::value::MINIMUM)
                .addPreset(75)
                    .addAssetId(alexa::value::MEDIUM)
                .addPreset(90)
                    .addAssetId(alexa::value::HIGH)
                    .addAssetId(alexa::value::MAXIMUM)
        .createEndpoint("secondRow.heater")
            .addAssetId(alexa::device::HEATER)
            .addAssetId(alexa::device::COOLER)
            .addPowerController(false)
            .addRangeController(
                "temperature", false, 60, 90, 1, alexa::unit::FAHRENHEIT)
                .addAssetId(alexa::setting::TEMPERATURE)
                .addAssetId(alexa::setting::HEAT)
                .addPreset(60)
                    .addAssetId(alexa::value::LOW)
                    .addAssetId(alexa::value::MINIMUM)
                .addPreset(75)
                    .addAssetId(alexa::value::MEDIUM)
                .addPreset(90)
                    .addAssetId(alexa::value::HIGH)
                    .addAssetId(alexa::value::MAXIMUM)
        //---------------------------------------------------------------------
        // Create a "window" endpoint for the driver zone. 
        // This endpoint includes a semantic action mapping for more natural 
        // voice targeting.
        //
        // Things to try:
        //    Without semantic action mappings...
        //      "Alexa, set the driver window height to [low|medium|high]""
        //    With semantic action mappings...
        //      "Alexa, [open|close|raise|lower] the driver window"
        //---------------------------------------------------------------------
        .createEndpoint("driver.window")
            .addAssetId(alexa::device::WINDOW)
            .addRangeController("height", false, 0, 100, 1)
                .addAssetId(alexa::setting::HEIGHT)
                .addPreset(0)
                    .addAssetId(alexa::value::LOW)
                    .addAssetId(alexa::value::MINIMUM)
                .addPreset(50)
                    .addAssetId(alexa::value::MEDIUM)
                .addPreset(100)
                    .addAssetId(alexa::value::HIGH)
                    .addAssetId(alexa::value::MAXIMUM)
                .addActionSetRange({aace::carControl::config::action::OPEN}, 0)
                .addActionSetRange({aace::carControl::config::action::CLOSE}, 100)
                .addActionAdjustRange({aace::carControl::config::action::RAISE}, 10)
                .addActionAdjustRange({aace::carControl::config::action::LOWER}, -10) 
        //---------------------------------------------------------------------
        // Create generic "light" endpoints for various zones.
        //
        // Things to try:
        //    "Alexa, turn [on|off] the [driver|passenger|front|second row] light"
        //---------------------------------------------------------------------
        .createEndpoint("driver.light")
            .addAssetId(alexa::device::LIGHT)
            .addPowerController(false)
        .createEndpoint("passenger.light")
            .addAssetId(alexa::device::LIGHT)
            .addPowerController(false)
        .createEndpoint("front.light")
            .addAssetId(alexa::device::LIGHT)
            .addPowerController(false)
        .createEndpoint("secondRow.light")
            .addAssetId(alexa::device::LIGHT)
            .addPowerController(false)
        //---------------------------------------------------------------------
        // Create various additional endpoints for specialized "lights"
        //
        // Things to try:
        //    "Alexa, turn [on|off] the light"
        //    "Alexa, turn [on|off] the [dome|cabin|reading] light"
        //    "Alexa, set the ambient light to blue"
        //---------------------------------------------------------------------
        .createEndpoint("dome.light")
            .addAssetId(alexa::device::DOME_LIGHT)
            .addAssetId(alexa::device::CABIN_LIGHT)
            .addPowerController(false)
        .createEndpoint("reading.light")
            .addAssetId(alexa::device::READING_LIGHT)
            .addPowerController(false)
        .createEndpoint("ambient.light")
            .addAssetId(alexa::device::AMBIENT_LIGHT)
            .addPowerController(false)
            .addModeController("color", false, true)
                .addAssetId(alexa::setting::COLOR)
                .addAssetId(alexa::setting::MODE)
                .addValue("RED")
                    .addAssetId(alexa::color::RED)
                .addValue("BLUE")
                    .addAssetId(alexa::color::BLUE)
                .addValue("GREEN")
                    .addAssetId(alexa::color::GREEN)
                .addValue("WHITE")
                    .addAssetId(alexa::color::WHITE)
                .addValue("ORANGE")
                    .addAssetId(alexa::color::ORANGE)
                .addValue("YELLOW")
                    .addAssetId(alexa::color::YELLOW)
                .addValue("INDIGO")
                    .addAssetId(alexa::color::INDIGO)
                .addValue("VIOLET")
                    .addAssetId(alexa::color::VIOLET)
        //---------------------------------------------------------------------
        // Create an "air conditioner" endpoint.
        //
        // Things to try:
        //    "Alexa, turn [on|off] the [air conditioner|AC]"
        //    "Alexa, set the AC mode to [economy|auto|manual]"
        //    "Alexa, set the AC intensity to [min|low|medium|high|max]"
        //    "Alexa, [increase|decrease] the AC"
        //    "Alexa, [raise|lower]" the AC
        //---------------------------------------------------------------------
        .createEndpoint("ac")
            .addAssetId(alexa::device::AIR_CONDITIONER)
            .addPowerController(false)
            .addModeController("mode", false, false)
                .addAssetId(alexa::setting::MODE)
                .addValue("ECONOMY")
                    .addAssetId(alexa::setting::ECONOMY)
                .addValue("AUTO")
                    .addAssetId(alexa::setting::AUTO)
                .addValue("MANUAL")
                    .addAssetId(alexa::setting::MANUAL)
            .addModeController("intensity", false, true)
                .addAssetId(alexa::setting::INTENSITY)
                .addValue("LOW")
                    .addAssetId(alexa::value::LOW)
                    .addAssetId(alexa::value::MINIMUM)
                .addValue("MEDIUM")
                    .addAssetId(alexa::value::MEDIUM)
                .addValue("HIGH")
                    .addAssetId(alexa::value::HIGH)
                    .addAssetId(alexa::value::MAXIMUM)
                .addActionAdjustMode({aace::carControl::config::action::RAISE}, 1)
                .addActionAdjustMode({aace::carControl::config::action::LOWER}, -1)
        //---------------------------------------------------------------------
        // Create a "windshield" endpoint for the rear zone.
        //
        // Things to try:
        //    "Alexa, turn [on|off] the rear windshield defroster"
        //---------------------------------------------------------------------
        .createEndpoint("rear.windshield")
            .addAssetId(alexa::device::WINDSHIELD)
            .addAssetId(alexa::device::WINDOW)
            .addToggleController("defroster", false)
                .addAssetId(alexa::setting::DEFROST)
        //---------------------------------------------------------------------
        // Create a "vent" endpoint. 
        // This endpoint includes a semantic action mapping for more natural 
        // voice targeting.
        //
        // Things to try:
        //    "Alexa, turn [on|off] the vent"
        //    "Alexa, set the vent position to [floor|body|mix]"
        //    "Alexa, [open|close|raise|lower] the vent"
        //---------------------------------------------------------------------
        .createEndpoint("vent")
            .addAssetId(alexa::device::VENT)
            .addPowerController(true)
            .addModeController("position", false, true)
                .addAssetId(alexa::setting::POSITION)
                .addValue("BODY")
                    .addAssetId(alexa::setting::BODY_VENTS)
                .addValue("FLOOR")
                    .addAssetId(alexa::setting::FLOOR_VENTS)
                .addValue("WINDSHIELD")
                    .addAssetId(alexa::setting::WINDSHIELD_VENTS)
                .addValue("MIX")
                    .addAssetId(alexa::setting::MIX_VENTS)
            .addToggleController("height", false)
                .addAssetId(alexa::setting::POSITION)
                .addActionTurnOn({aace::carControl::config::action::OPEN, 
                    aace::carControl::config::action::RAISE})
                .addActionTurnOff({aace::carControl::config::action::CLOSE, 
                    aace::carControl::config::action::LOWER})
        //---------------------------------------------------------------------
        // Create "seat heater" endpoints for driver and passenger zones.
        //
        // Things to try:
        //    "Alexa, turn [on|off] the [driver|passenger] seat heater"
        //    "Alexa, set the [driver|passenger] seat heater intensity to 
        //          [low|minimum|medium|high|max]"
        //    "Alexa, set the [driver|passenger] seat heater to 
        //          <value between 1 and 3>"
        //    "Alexa, [turn up|increase|decrease] the [driver|passenger] seat 
        //          heater"
        //---------------------------------------------------------------------  
        .createEndpoint("driver.seat")
            .addAssetId(alexa::device::SEAT)
            .addToggleController("heater", false)
                .addAssetId(alexa::device::HEATER)
                .addAssetId(alexa::setting::HEAT)
            .addRangeController("heaterintensity", false, 1, 3, 1)
                .addAssetId(alexa::device::HEATER)
                .addAssetId(alexa::setting::HEAT)
                .addPreset(1)
                    .addAssetId(alexa::value::LOW)
                    .addAssetId(alexa::value::MINIMUM)
                .addPreset(2)
                    .addAssetId(alexa::value::MEDIUM)
                .addPreset(3)
                    .addAssetId(alexa::value::HIGH)
                    .addAssetId(alexa::value::MAXIMUM)
        .createEndpoint("passenger.seat")
            .addAssetId(alexa::device::SEAT)
            .addToggleController("heater", false)
                .addAssetId(alexa::device::HEATER)
                .addAssetId(alexa::setting::HEAT)
            .addRangeController("heaterintensity", false, 1, 3, 1)
                .addAssetId(alexa::device::HEATER)
                .addAssetId(alexa::setting::HEAT)
                .addPreset(1)
                    .addAssetId(alexa::value::LOW)
                    .addAssetId(alexa::value::MINIMUM)
                .addPreset(2)
                    .addAssetId(alexa::value::MEDIUM)
                .addPreset(3)
                    .addAssetId(alexa::value::HIGH)
                    .addAssetId(alexa::value::MAXIMUM)
        //---------------------------------------------------------------------
        // Create a "car" root endpoint for miscellaneous controls not
        // associated with any other endpoint.
        //
        // Things to try:
        //    "Alexa, turn [on|off] air recirculation"
        //    "Alexa, turn [on|off] climate sync"
        //---------------------------------------------------------------------
        .createEndpoint("car")
            .addAssetId(alexa::device::CAR)
            .addToggleController("recirculate", false)
                .addAssetId(alexa::setting::AIR_RECIRCULATION)
            .addToggleController("climate.sync", false)
                .addAssetId(alexa::setting::CLIMATE_SYNC);
    // clang-format on
    return config;
}

}  // namespace carControl
}  // namespace sampleApp