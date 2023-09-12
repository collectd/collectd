/**
 * collectd - src/redfish_test.c
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 * Copyright(c) 2021 Atos. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Original authors:
 *   Martin Kennelly <martin.kennelly@intel.com>
 *   Marcin Mozejko <marcinx.mozejko@intel.com>
 *   Adrian Boczkowski <adrianx.boczkowski@intel.com>
 *
 * Refactoring and enhancement author:
 *      Mathieu Stoffel <mathieu.stoffel@atos.net>
 **/
/* So as to use the "test-dedicated" version of the source code of the Redfish
 * plugin: */
#define REDFISH_PLUGIN_TEST

#include <math.h>

#include "redfish.c"
#include "testing.h"

/******************************************************************************
 * Mocking of the type/data source inference interface:
 ******************************************************************************/
#define DS_FANSPEED_NUM_DSOURCES (1)
/***/
static data_source_t fanspeed_dsources[DS_FANSPEED_NUM_DSOURCES] = {
    {.name = "value", .type = DS_TYPE_GAUGE, .min = 0.0, .max = NAN}};
/***/
static data_set_t fanspeed_dset = {
    .type = "fanspeed", .ds_num = 1, .ds = fanspeed_dsources};

/*******/

#define DS_VOLTAGE_NUM_DSOURCES (1)
/***/
static data_source_t voltage_dsources[DS_VOLTAGE_NUM_DSOURCES] = {
    {.name = "value", .type = DS_TYPE_GAUGE, .min = NAN, .max = NAN}};
/***/
static data_set_t voltage_dset = {
    .type = "voltage", .ds_num = 1, .ds = voltage_dsources};

/*******/

#define DS_TEMPERATURE_NUM_DSOURCES (1)
/***/
static data_source_t temperature_dsources[DS_TEMPERATURE_NUM_DSOURCES] = {
    {.name = "value", .type = DS_TYPE_GAUGE, .min = NAN, .max = NAN}};
/***/
static data_set_t temperature_dset = {
    .type = "temperature", .ds_num = 1, .ds = temperature_dsources};

/*******/

#define DS_CAPACITY_NUM_DSOURCES (1)
/***/
static data_source_t capacity_dsources[DS_CAPACITY_NUM_DSOURCES] = {
    {.name = "value", .type = DS_TYPE_GAUGE, .min = 0.0, .max = NAN}};
/***/
static data_set_t capacity_dset = {
    .type = "capacity", .ds_num = 1, .ds = capacity_dsources};

/*******/

/* Mocking the type/data source inference: */
static const data_set_t *
redfish_test_plugin_get_ds_mock(const char *const type) {
  /* Determining which type was specified, and returning the associated data
   * set: */
  if (strcasecmp(type, "fanspeed") == 0)
    return (&fanspeed_dset);
  else if (strcasecmp(type, "voltage") == 0)
    return (&voltage_dset);
  else if (strcasecmp(type, "temperature") == 0)
    return (&temperature_dset);
  else if (strcasecmp(type, "capacity") == 0)
    return (&capacity_dset);

  return NULL;
}

/******************************************************************************
 * Mocking of the dispatching interface:
 ******************************************************************************/
/* List, used as a queue, of the lastly dispatched values: */
static llist_t *last_dispatched_values_list;

/*******/

/* Mocking the dispatch of sampled values: */
static int
redfish_test_plugin_dispatch_values_mock(const value_list_t *dispatched_vl) {
  /* Allocating a new "value_lisit_t" so as to deep-copy "dispatched_vl": */
  value_list_t *vl = malloc(sizeof(*vl));
  /***/
  if (vl == NULL)
    return EXIT_FAILURE;
  /***/
  memset(vl, 0, sizeof(*vl));

  /* Performing the deep-copy of "dispatched_vl" into "vl" (which includes the
   * (potential) allocation of "vl->values"): */
  sstrncpy(vl->plugin, dispatched_vl->plugin, sizeof(dispatched_vl->plugin));
  /***/
  sstrncpy(vl->host, dispatched_vl->host, sizeof(dispatched_vl->host));
  /***/
  sstrncpy(vl->plugin_instance, dispatched_vl->plugin_instance,
           sizeof(dispatched_vl->plugin_instance));
  /***/
  sstrncpy(vl->type, dispatched_vl->type, sizeof(dispatched_vl->type));
  /***/
  sstrncpy(vl->type_instance, dispatched_vl->type_instance,
           sizeof(dispatched_vl->type_instance));
  /***/
  vl->values_len = dispatched_vl->values_len;
  /***/
  vl->values = malloc(vl->values_len * sizeof(*(vl->values)));
  /****/
  if (vl->values == NULL) {
    free(vl);
    return EXIT_FAILURE;
  }
  /****/
  memcpy(vl->values, dispatched_vl->values,
         vl->values_len * sizeof(*(vl->values)));

  /* Allocating a new entry for these dispatched values to be inserted into
   * "last_dispatched_values_list": */
  llentry_t *le = llentry_create("", vl);
  /***/
  if (le == NULL) {
    free(vl->values);
    free(vl);
  }
  /***/
  llist_append(last_dispatched_values_list, le);

  return EXIT_SUCCESS;
}

/*******/

/* Getting the next dispatched values to be consumed, using the associated list
 * of dispatched values as a queue: */
static value_list_t *redfish_test_get_next_dispatched_values() {
  return (llist_head(last_dispatched_values_list)->value);
}

/*******/

/* Removes the head values from the list of dispatched values to notify that it
 * has been consumed: */
static void redfish_test_remove_next_dispatched_values() {
  llentry_t *head = llist_head(last_dispatched_values_list);

  value_list_t *vl = head->value;
  /***/
  if (vl != NULL) {
    if (vl->values != NULL)
      free(vl->values);
    free(vl);
  }

  llist_remove(last_dispatched_values_list, head);
  llentry_destroy(head);
}

/******************************************************************************
 * In-memory configuration file:
 ******************************************************************************/
/* To be allocated to build a configuration file: */
static oconfig_item_t *config_file = NULL;

/* Pointers to specific sub-parts of interest of the configuration file built
 * in memory: */
#define CONFIG_FILE_SERVICES (1)
#define CONFIG_FILE_QUERIES (5)
#define CONFIG_FILE_SUBPARTS (CONFIG_FILE_SERVICES + CONFIG_FILE_QUERIES)
static oconfig_item_t *cf_service = NULL;
static oconfig_item_t *cf_query_thermal = NULL;
static oconfig_item_t *cf_query_voltages = NULL;
static oconfig_item_t *cf_query_temperatures = NULL;
static oconfig_item_t *cf_query_ps1_voltage = NULL;
static oconfig_item_t *cf_query_storage = NULL;

/*******/

/* In order to test the plugin by hand during its implementation, the "Simple
 * Rack-mounted Server" mockup developed by the DMTF was used:
 *          https://redfish.dmtf.org/redfish/mockups/v1/1100
 *
 * To be complete, it was emulated through the "Redfish-Interface-Emulator":
 *          https://github.com/DMTF/Redfish-Interface-Emulator
 *
 * Consequently, it seemed appropriated to use this same mockup for the
 * automated test suite associated with the plugin.
 * To do so:
 *      + An in-memory copy of the configuration file used to collect metrics
 *        from the emulated mockup was built through "oconfig";
 *      + The JSON payloads associated with the queries defined by the
 *        aforementioned configuration file were replicated.
 */
static char *json_payloads[CONFIG_FILE_QUERIES] = {
    "{\n"
    "\"@odata.type\": \"#Thermal.v1_7_0.Thermal\",\n"
    "\"Id\": \"Thermal\",\n"
    "\"Name\": \"Thermal\",\n"
    "\"Temperatures\": [\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U/Thermal#/Temperatures/0\",\n"
    "\"MemberId\": \"0\",\n"
    "\"Name\": \"CPU1 Temp\",\n"
    "\"SensorNumber\": 5,\n"
    "\"Status\": {\n"
    "\"State\": \"Enabled\",\n"
    "\"Health\": \"OK\"\n"
    "},\n"
    "\"ReadingCelsius\": 41,\n"
    "\"UpperThresholdNonCritical\": 42,\n"
    "\"UpperThresholdCritical\": 45,\n"
    "\"UpperThresholdFatal\": 48,\n"
    "\"MinReadingRangeTemp\": 0,\n"
    "\"MaxReadingRangeTemp\": 60,\n"
    "\"PhysicalContext\": \"CPU\",\n"
    "\"RelatedItem\": [\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Systems/437XR1138R2/Processors/CPU1\"\n"
    "}\n"
    "]\n"
    "},\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U/Thermal#/Temperatures/1\",\n"
    "\"MemberId\": \"1\",\n"
    "\"Name\": \"CPU2 Temp\",\n"
    "\"SensorNumber\": 6,\n"
    "\"Status\": {\n"
    "\"State\": \"Disabled\"\n"
    "},\n"
    "\"UpperThresholdNonCritical\": 42,\n"
    "\"UpperThresholdCritical\": 45,\n"
    "\"UpperThresholdFatal\": 48,\n"
    "\"MinReadingRangeTemp\": 0,\n"
    "\"MaxReadingRangeTemp\": 60,\n"
    "\"PhysicalContext\": \"CPU\",\n"
    "\"RelatedItem\": [\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Systems/437XR1138R2/Processors/CPU2\"\n"
    "}\n"
    "]\n"
    "},\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U/Thermal#/Temperatures/2\",\n"
    "\"MemberId\": \"2\",\n"
    "\"Name\": \"Chassis Intake Temp\",\n"
    "\"SensorNumber\": 9,\n"
    "\"Status\": {\n"
    "\"State\": \"Enabled\",\n"
    "\"Health\": \"OK\"\n"
    "},\n"
    "\"ReadingCelsius\": 25,\n"
    "\"UpperThresholdNonCritical\": 30,\n"
    "\"UpperThresholdCritical\": 40,\n"
    "\"UpperThresholdFatal\": 50,\n"
    "\"LowerThresholdNonCritical\": 10,\n"
    "\"LowerThresholdCritical\": 5,\n"
    "\"LowerThresholdFatal\": 0,\n"
    "\"MinReadingRangeTemp\": 0,\n"
    "\"MaxReadingRangeTemp\": 60,\n"
    "\"PhysicalContext\": \"Intake\",\n"
    "\"RelatedItem\": [\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U\"\n"
    "},\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Systems/437XR1138R2\"\n"
    "}\n"
    "]\n"
    "}\n"
    "],\n"
    "\"Fans\": [\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U/Thermal#/Fans/0\",\n"
    "\"MemberId\": \"0\",\n"
    "\"Name\": \"BaseBoard System Fan\",\n"
    "\"PhysicalContext\": \"Backplane\",\n"
    "\"Status\": {\n"
    "\"State\": \"Enabled\",\n"
    "\"Health\": \"OK\"\n"
    "},\n"
    "\"Reading\": 2100,\n"
    "\"ReadingUnits\": \"RPM\",\n"
    "\"LowerThresholdFatal\": 0,\n"
    "\"MinReadingRange\": 0,\n"
    "\"MaxReadingRange\": 5000,\n"
    "\"Redundancy\": [\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U/Thermal#/Redundancy/0\"\n"
    "}\n"
    "],\n"
    "\"RelatedItem\": [\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Systems/437XR1138R2\"\n"
    "},\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U\"\n"
    "}\n"
    "]\n"
    "},\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U/Thermal#/Fans/1\",\n"
    "\"MemberId\": \"1\",\n"
    "\"Name\": \"BaseBoard System Fan Backup\",\n"
    "\"PhysicalContext\": \"Backplane\",\n"
    "\"Status\": {\n"
    "\"State\": \"Enabled\",\n"
    "\"Health\": \"OK\"\n"
    "},\n"
    "\"Reading\": 2050,\n"
    "\"ReadingUnits\": \"RPM\",\n"
    "\"LowerThresholdFatal\": 0,\n"
    "\"MinReadingRange\": 0,\n"
    "\"MaxReadingRange\": 5000,\n"
    "\"Redundancy\": [\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U/Thermal#/Redundancy/0\"\n"
    "}\n"
    "],\n"
    "\"RelatedItem\": [\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Systems/437XR1138R2\"\n"
    "},\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U\"\n"
    "}\n"
    "]\n"
    "}\n"
    "],\n"
    "\"Redundancy\": [\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U/Thermal#/Redundancy/0\",\n"
    "\"MemberId\": \"0\",\n"
    "\"Name\": \"BaseBoard System Fans\",\n"
    "\"RedundancySet\": [\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U/Thermal#/Fans/0\"\n"
    "},\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U/Thermal#/Fans/1\"\n"
    "}\n"
    "],\n"
    "\"Mode\": \"N+m\",\n"
    "\"Status\": {\n"
    "\"State\": \"Enabled\",\n"
    "\"Health\": \"OK\"\n"
    "},\n"
    "\"MinNumNeeded\": 1,\n"
    "\"MaxNumSupported\": 2\n"
    "}\n"
    "],\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U/Thermal\",\n"
    "\"@Redfish.Copyright\": \"Copyright 2014-2021 DMTF. For the full DMTF "
    "copyright policy, see http://www.dmtf.org/about/policies/copyright.\"\n"
    "}",
    "{\n"
    "\"@odata.type\": \"#Power.v1_7_0.Power\",\n"
    "\"Id\": \"Power\",\n"
    "\"Name\": \"Power\",\n"
    "\"PowerControl\": [\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U/Power#/PowerControl/0\",\n"
    "\"MemberId\": \"0\",\n"
    "\"Name\": \"System Input Power\",\n"
    "\"PowerConsumedWatts\": 344,\n"
    "\"PowerRequestedWatts\": 800,\n"
    "\"PowerAvailableWatts\": 0,\n"
    "\"PowerCapacityWatts\": 800,\n"
    "\"PowerAllocatedWatts\": 800,\n"
    "\"PowerMetrics\": {\n"
    "\"IntervalInMin\": 30,\n"
    "\"MinConsumedWatts\": 271,\n"
    "\"MaxConsumedWatts\": 489,\n"
    "\"AverageConsumedWatts\": 319\n"
    "},\n"
    "\"PowerLimit\": {\n"
    "\"LimitInWatts\": 500,\n"
    "\"LimitException\": \"LogEventOnly\",\n"
    "\"CorrectionInMs\": 50\n"
    "},\n"
    "\"RelatedItem\": [\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Systems/437XR1138R2\"\n"
    "},\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U\"\n"
    "}\n"
    "],\n"
    "\"Status\": {\n"
    "\"State\": \"Enabled\",\n"
    "\"Health\": \"OK\"\n"
    "},\n"
    "\"Oem\": {}\n"
    "}\n"
    "],\n"
    "\"Voltages\": [\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U/Power#/Voltages/0\",\n"
    "\"MemberId\": \"0\",\n"
    "\"Name\": \"VRM1 Voltage\",\n"
    "\"SensorNumber\": 11,\n"
    "\"Status\": {\n"
    "\"State\": \"Enabled\",\n"
    "\"Health\": \"OK\"\n"
    "},\n"
    "\"ReadingVolts\": 12,\n"
    "\"UpperThresholdNonCritical\": 12.5,\n"
    "\"UpperThresholdCritical\": 13,\n"
    "\"UpperThresholdFatal\": 15,\n"
    "\"LowerThresholdNonCritical\": 11.5,\n"
    "\"LowerThresholdCritical\": 11,\n"
    "\"LowerThresholdFatal\": 10,\n"
    "\"MinReadingRange\": 0,\n"
    "\"MaxReadingRange\": 20,\n"
    "\"PhysicalContext\": \"VoltageRegulator\",\n"
    "\"RelatedItem\": [\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Systems/437XR1138R2\"\n"
    "},\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U\"\n"
    "}\n"
    "]\n"
    "},\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U/Power#/Voltages/1\",\n"
    "\"MemberId\": \"1\",\n"
    "\"Name\": \"VRM2 Voltage\",\n"
    "\"SensorNumber\": 12,\n"
    "\"Status\": {\n"
    "\"State\": \"Enabled\",\n"
    "\"Health\": \"OK\"\n"
    "},\n"
    "\"ReadingVolts\": 5,\n"
    "\"UpperThresholdNonCritical\": 5.5,\n"
    "\"UpperThresholdCritical\": 7,\n"
    "\"LowerThresholdNonCritical\": 4.75,\n"
    "\"LowerThresholdCritical\": 4.5,\n"
    "\"MinReadingRange\": 0,\n"
    "\"MaxReadingRange\": 20,\n"
    "\"PhysicalContext\": \"VoltageRegulator\",\n"
    "\"RelatedItem\": [\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Systems/437XR1138R2\"\n"
    "},\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U\"\n"
    "}\n"
    "]\n"
    "}\n"
    "],\n"
    "\"PowerSupplies\": [\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U/Power#/PowerSupplies/0\",\n"
    "\"MemberId\": \"0\",\n"
    "\"Name\": \"Power Supply Bay\",\n"
    "\"Status\": {\n"
    "\"State\": \"Enabled\",\n"
    "\"Health\": \"Warning\"\n"
    "},\n"
    "\"Oem\": {},\n"
    "\"PowerSupplyType\": \"AC\",\n"
    "\"LineInputVoltageType\": \"ACWideRange\",\n"
    "\"LineInputVoltage\": 120,\n"
    "\"PowerCapacityWatts\": 800,\n"
    "\"LastPowerOutputWatts\": 325,\n"
    "\"Model\": \"499253-B21\",\n"
    "\"Manufacturer\": \"ManufacturerName\",\n"
    "\"FirmwareVersion\": \"1.00\",\n"
    "\"SerialNumber\": \"1Z0000001\",\n"
    "\"PartNumber\": \"0000001A3A\",\n"
    "\"SparePartNumber\": \"0000001A3A\",\n"
    "\"InputRanges\": [\n"
    "{\n"
    "\"InputType\": \"AC\",\n"
    "\"MinimumVoltage\": 100,\n"
    "\"MaximumVoltage\": 120,\n"
    "\"OutputWattage\": 800\n"
    "},\n"
    "{\n"
    "\"InputType\": \"AC\",\n"
    "\"MinimumVoltage\": 200,\n"
    "\"MaximumVoltage\": 240,\n"
    "\"OutputWattage\": 1300\n"
    "}\n"
    "],\n"
    "\"RelatedItem\": [\n"
    "{\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U\"\n"
    "}\n"
    "]\n"
    "}\n"
    "],\n"
    "\"Oem\": {},\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U/Power\",\n"
    "\"@Redfish.Copyright\": \"Copyright 2014-2021 DMTF. For the full DMTF "
    "copyright policy, see http://www.dmtf.org/about/policies/copyright.\"\n"
    "}",
    "{\n"
    "\"@odata.type\": \"#ThermalMetrics.v1_0_0.ThermalMetrics\",\n"
    "\"Id\": \"ThermalMetrics\",\n"
    "\"Name\": \"Chassis Thermal Metrics\",\n"
    "\"TemperatureSummaryCelsius\": {\n"
    "\"Internal\": {\n"
    "\"Reading\": 39,\n"
    "\"DataSourceUri\": \"/redfish/v1/Chassis/1U/Sensors/CPU1Temp\"\n"
    "},\n"
    "\"Intake\": {\n"
    "\"Reading\": 24.8,\n"
    "\"DataSourceUri\": \"/redfish/v1/Chassis/1U/Sensors/IntakeTemp\"\n"
    "},\n"
    "\"Ambient\": {\n"
    "\"Reading\": 22.5,\n"
    "\"DataSourceUri\": \"/redfish/v1/Chassis/1U/Sensors/AmbientTemp\"\n"
    "},\n"
    "\"Exhaust\": {\n"
    "\"Reading\": 40.5,\n"
    "\"DataSourceUri\": \"/redfish/v1/Chassis/1U/Sensors/ExhaustTemp\"\n"
    "}\n"
    "},\n"
    "\"TemperatureReadingsCelsius\": [\n"
    "{\n"
    "\"Reading\": 24.8,\n"
    "\"DeviceName\": \"Intake\",\n"
    "\"DataSourceUri\": \"/redfish/v1/Chassis/1U/Sensors/IntakeTemp\"\n"
    "},\n"
    "{\n"
    "\"Reading\": 40.5,\n"
    "\"DeviceName\": \"Exhaust\",\n"
    "\"DataSourceUri\": \"/redfish/v1/Chassis/1U/Sensors/ExhaustTemp\"\n"
    "}\n"
    "],\n"
    "\"Oem\": {},\n"
    "\"@odata.id\": "
    "\"/redfish/v1/Chassis/1U/ThermalSubsystem/ThermalMetrics\",\n"
    "\"@Redfish.Copyright\": \"Copyright 2014-2021 DMTF. For the full DMTF "
    "copyright policy, see http://www.dmtf.org/about/policies/copyright.\"\n"
    "}\n",
    "{\n"
    "\"@odata.type\": \"#Sensor.v1_2_0.Sensor\",\n"
    "\"Id\": \"PS1InputVoltage\",\n"
    "\"Name\": \"Power Supply #1 Input Voltage\",\n"
    "\"ReadingType\": \"Voltage\",\n"
    "\"Status\": {\n"
    "\"State\": \"Enabled\",\n"
    "\"Health\": \"OK\"\n"
    "},\n"
    "\"ElectricalContext\": \"Total\",\n"
    "\"Reading\": 119.27,\n"
    "\"ReadingUnits\": \"V\",\n"
    "\"ReadingRangeMin\": 0,\n"
    "\"ReadingRangeMax\": 260,\n"
    "\"Accuracy\": 0.02,\n"
    "\"Precision\": 2,\n"
    "\"SensingInterval\": \"PT0.125S\",\n"
    "\"PhysicalContext\": \"PowerSupply\",\n"
    "\"PhysicalSubContext\": \"Input\",\n"
    "\"Thresholds\": {\n"
    "\"UpperCritical\": {\n"
    "\"Reading\": 125,\n"
    "\"Activation\": \"Increasing\",\n"
    "\"DwellTime\": \"PT1M\"\n"
    "},\n"
    "\"UpperCaution\": {\n"
    "\"Reading\": 122,\n"
    "\"DwellTime\": \"PT10M\"\n"
    "},\n"
    "\"LowerCaution\": {\n"
    "\"Reading\": 118,\n"
    "\"DwellTime\": \"PT5M\"\n"
    "},\n"
    "\"LowerCritical\": {\n"
    "\"Reading\": 115,\n"
    "\"DwellTime\": \"PT1M\"\n"
    "}\n"
    "},\n"
    "\"Oem\": {},\n"
    "\"@odata.id\": \"/redfish/v1/Chassis/1U/Sensors/PS1InputVoltage\",\n"
    "\"@Redfish.Copyright\": \"Copyright 2014-2021 DMTF. For the full DMTF "
    "copyright policy, see http://www.dmtf.org/about/policies/copyright.\"\n"
    "}\n",
    "{\n"
    "\"@odata.type\": \"#SimpleStorage.v1_0_2.SimpleStorage\",\n"
    "\"Id\": \"1\",\n"
    "\"Name\": \"Simple Storage Controller\",\n"
    "\"Description\": \"System SATA\",\n"
    "\"UEFIDevicePath\": "
    "\"Acpi(PNP0A03,0)/Pci(1F|1)/Ata(Primary,Master)/HD(Part3, "
    "Sig00110011)\",\n"
    "\"Status\": {\n"
    "\"State\": \"Enabled\",\n"
    "\"Health\": \"OK\",\n"
    "\"HealthRollUp\": \"Degraded\"\n"
    "},\n"
    "\"Devices\": [\n"
    "{\n"
    "\"Name\": \"SATA Bay 1\",\n"
    "\"Manufacturer\": \"Contoso\",\n"
    "\"Model\": \"3000GT8\",\n"
    "\"CapacityBytes\": 8000000000000,\n"
    "\"Status\": {\n"
    "\"State\": \"Enabled\",\n"
    "\"Health\": \"OK\"\n"
    "}\n"
    "},\n"
    "{\n"
    "\"Name\": \"SATA Bay 2\",\n"
    "\"Manufacturer\": \"Contoso\",\n"
    "\"Model\": \"3000GT7\",\n"
    "\"CapacityBytes\": 4000000000000,\n"
    "\"Status\": {\n"
    "\"State\": \"Enabled\",\n"
    "\"Health\": \"Degraded\"\n"
    "}\n"
    "},\n"
    "{\n"
    "\"Name\": \"SATA Bay 3\",\n"
    "\"Status\": {\n"
    "\"State\": \"Absent\"\n"
    "}\n"
    "},\n"
    "{\n"
    "\"Name\": \"SATA Bay 4\",\n"
    "\"Status\": {\n"
    "\"State\": \"Absent\"\n"
    "}\n"
    "}\n"
    "],\n"
    "\"@odata.context\": "
    "\"/redfish/v1/$metadata#Systems/Members/437XR1138R2/SimpleStorage/Members/"
    "$entity\",\n"
    "\"@odata.id\": \"/redfish/v1/Systems/437XR1138R2/SimpleStorage/1\",\n"
    "\"@Redfish.Copyright\": \"Copyright 2014-2016 DMTF. For the full DMTF "
    "copyright policy, see http://www.dmtf.org/about/policies/copyright.\"\n"
    "}\n"};

/*******/

/* Builds a configuration file in memory so as to be able to properly test each
 * individual feature of the plugin.
 * The built configuration file is to be cleaned up by "destroy_config_file": */
static uint64_t build_config_file(void) {
  /* Utilitary pointers to be used later on in order to make the code
   * associated with the building of the configuration file clearer: */
  oconfig_item_t *cf_resource = NULL;
  oconfig_item_t *cf_property = NULL;
  oconfig_item_t *cf_attribute = NULL;

  /**************************************************************************
   * Root:
   **************************************************************************/
  /* Allocating the root of the configuration file: */
  config_file = calloc(1, sizeof(*config_file));
  if (config_file == NULL)
    return EXIT_FAILURE;
  /***/
  config_file->key = "redfish";
  config_file->parent = NULL;

  /* Allocating the queries and services belonging to the configuration file,
   * which are the children of its root: */
  config_file->children_num = CONFIG_FILE_SUBPARTS;
  /***/
  config_file->children =
      calloc(config_file->children_num, sizeof(*(config_file->children)));
  /***/
  if (config_file->children == NULL)
    return EXIT_FAILURE;

  /* Assigning the pointers specific to sub-parts of the configuration
   * files: */
  cf_service = &(config_file->children[0]);
  cf_query_thermal = &(config_file->children[1]);
  cf_query_voltages = &(config_file->children[2]);
  cf_query_temperatures = &(config_file->children[3]);
  cf_query_ps1_voltage = &(config_file->children[4]);
  cf_query_storage = &(config_file->children[5]);

  /**************************************************************************
   * Service subpart:
   **************************************************************************/
  /* Allocating the root of the Service subpart of the configuration file: */
  cf_service->key = "Service";
  cf_service->parent = config_file;
  /***/
  cf_service->values_num = 1;
  cf_service->values =
      calloc(cf_service->values_num, sizeof(*(cf_service->values)));
  /***/
  if (cf_service->values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_service->values[0].type = OCONFIG_TYPE_STRING;
  cf_service->values[0].value.string = "mock1U";

  /* Allocating the children of the Service subpart: */
  cf_service->children_num = 4;
  cf_service->children =
      calloc(cf_service->children_num, sizeof(*(cf_service->children)));
  /***/
  if (cf_service->children == NULL)
    return EXIT_FAILURE;

  /* Filling in the information related to the host: */
  cf_service->children[0].key = "Host";
  cf_service->children[0].parent = cf_service;
  /***/
  cf_service->children[0].values_num = 1;
  cf_service->children[0].values =
      calloc(cf_service->children[0].values_num,
             sizeof(*(cf_service->children[0].values)));
  /***/
  if (cf_service->children[0].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_service->children[0].values[0].type = OCONFIG_TYPE_STRING;
  cf_service->children[0].values[0].value.string = "localhost:10000";

  /* Filling in the information related to the user: */
  cf_service->children[1].key = "User";
  cf_service->children[1].parent = cf_service;
  /***/
  cf_service->children[1].values_num = 1;
  cf_service->children[1].values =
      calloc(cf_service->children[1].values_num,
             sizeof(*(cf_service->children[1].values)));
  /***/
  if (cf_service->children[1].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_service->children[1].values[0].type = OCONFIG_TYPE_STRING;
  cf_service->children[1].values[0].value.string = "";

  /* Filling in the information related to the password: */
  cf_service->children[2].key = "Passwd";
  cf_service->children[2].parent = cf_service;
  /***/
  cf_service->children[2].values_num = 1;
  cf_service->children[2].values =
      calloc(cf_service->children[2].values_num,
             sizeof(*(cf_service->children[2].values)));
  /***/
  if (cf_service->children[2].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_service->children[2].values[0].type = OCONFIG_TYPE_STRING;
  cf_service->children[2].values[0].value.string = "";

  /* Filling in the information related to the queries: */
  cf_service->children[3].key = "Queries";
  cf_service->children[3].parent = cf_service;
  /***/
  cf_service->children[3].values_num = CONFIG_FILE_QUERIES;
  cf_service->children[3].values =
      calloc(cf_service->children[3].values_num,
             sizeof(*(cf_service->children[3].values)));
  /***/
  if (cf_service->children[3].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_service->children[3].values[0].type = OCONFIG_TYPE_STRING;
  cf_service->children[3].values[0].value.string = "thermal";
  /***/
  cf_service->children[3].values[1].type = OCONFIG_TYPE_STRING;
  cf_service->children[3].values[1].value.string = "voltages";
  /***/
  cf_service->children[3].values[2].type = OCONFIG_TYPE_STRING;
  cf_service->children[3].values[2].value.string = "temperatures";
  /***/
  cf_service->children[3].values[3].type = OCONFIG_TYPE_STRING;
  cf_service->children[3].values[3].value.string = "ps1_voltage";
  /***/
  cf_service->children[3].values[4].type = OCONFIG_TYPE_STRING;
  cf_service->children[3].values[4].value.string = "storage";

  /**************************************************************************
   * Query "fans" subpart:
   **************************************************************************/
  /* Allocating the root of the Query "thermal" subpart of the configuration
   * file: */
  cf_query_thermal->key = "Query";
  cf_query_thermal->parent = config_file;
  /***/
  cf_query_thermal->values_num = 1;
  cf_query_thermal->values =
      calloc(cf_query_thermal->values_num, sizeof(*(cf_query_thermal->values)));
  /***/
  if (cf_query_thermal->values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_query_thermal->values[0].type = OCONFIG_TYPE_STRING;
  cf_query_thermal->values[0].value.string = "thermal";

  /* Allocating the children of the Query "thermal" subpart: */
  cf_query_thermal->children_num = 3;
  cf_query_thermal->children = calloc(cf_query_thermal->children_num,
                                      sizeof(*(cf_query_thermal->children)));
  /***/
  if (cf_query_thermal->children == NULL)
    return EXIT_FAILURE;

  /* Filling in the information related to the endpoint: */
  cf_query_thermal->children[0].key = "Endpoint";
  cf_query_thermal->children[0].parent = cf_query_thermal;
  /***/
  cf_query_thermal->children[0].values_num = 1;
  cf_query_thermal->children[0].values =
      calloc(cf_query_thermal->children[0].values_num,
             sizeof(*(cf_query_thermal->children[0].values)));
  /***/
  if (cf_query_thermal->children[0].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_query_thermal->children[0].values[0].type = OCONFIG_TYPE_STRING;
  cf_query_thermal->children[0].values[0].value.string = "/Chassis[0]/Thermal";

  /* Filling in the information related to the resource: */
  cf_resource = &(cf_query_thermal->children[1]);
  cf_resource->key = "Resource";
  cf_resource->parent = cf_query_thermal;
  /***/
  cf_resource->values_num = 1;
  cf_resource->values =
      calloc(cf_resource->values_num, sizeof(*(cf_resource->values)));
  /***/
  if (cf_resource->values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_resource->values[0].type = OCONFIG_TYPE_STRING;
  cf_resource->values[0].value.string = "Fans";

  /* Allocating the property associated with the first resource: */
  cf_resource->children_num = 1;
  cf_resource->children =
      calloc(cf_resource->children_num, sizeof(*(cf_resource->children)));
  /***/
  if (cf_resource->children == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property = &(cf_resource->children[0]);

  /* Filling in the information related to the property: */
  cf_property->key = "Property";
  cf_property->parent = cf_resource;
  /***/
  cf_property->values_num = 1;
  cf_property->values =
      calloc(cf_property->values_num, sizeof(*(cf_property->values)));
  /***/
  if (cf_property->values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->values[0].type = OCONFIG_TYPE_STRING;
  cf_property->values[0].value.string = "Reading";

  /* Allocating the fields (i.e. children) of the property: */
  cf_property->children_num = 4;
  cf_property->children =
      calloc(cf_property->children_num, sizeof(*(cf_property->children)));
  /***/
  if (cf_property->children == NULL)
    return EXIT_FAILURE;

  /* Filling in the information related to the PluginInstance field of the
   * property: */
  cf_property->children[0].parent = cf_property;
  cf_property->children[0].key = "PluginInstance";
  /***/
  cf_property->children[0].values_num = 1;
  cf_property->children[0].values =
      calloc(cf_property->children[0].values_num,
             sizeof(*(cf_property->children[0].values)));
  /***/
  if (cf_property->children[0].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->children[0].values[0].type = OCONFIG_TYPE_STRING;
  cf_property->children[0].values[0].value.string = "Fans";

  /* Filling in the information related to the Type field of the property: */
  cf_property->children[1].parent = cf_property;
  cf_property->children[1].key = "Type";
  /***/
  cf_property->children[1].values_num = 1;
  cf_property->children[1].values =
      calloc(cf_property->children[1].values_num,
             sizeof(*(cf_property->children[1].values)));
  /***/
  if (cf_property->children[1].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->children[1].values[0].type = OCONFIG_TYPE_STRING;
  cf_property->children[1].values[0].value.string = "fanspeed";

  /* Filling in the information related to the TypeInstanceAttr field of the
   * property: */
  cf_property->children[2].parent = cf_property;
  cf_property->children[2].key = "TypeInstanceAttr";
  /***/
  cf_property->children[2].values_num = 1;
  cf_property->children[2].values =
      calloc(cf_property->children[2].values_num,
             sizeof(*(cf_property->children[2].values)));
  /***/
  if (cf_property->children[2].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->children[2].values[0].type = OCONFIG_TYPE_STRING;
  cf_property->children[2].values[0].value.string = "Name";

  /* Filling in the information related to the SelectIDs field of the
   * property: */
  cf_property->children[3].parent = cf_property;
  cf_property->children[3].key = "SelectIDs";
  /***/
  cf_property->children[3].values_num = 1;
  cf_property->children[3].values =
      calloc(cf_property->children[1].values_num,
             sizeof(*(cf_property->children[3].values)));
  /***/
  if (cf_property->children[3].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->children[3].values[0].type = OCONFIG_TYPE_NUMBER;
  cf_property->children[3].values[0].value.number = 1;

  /* Filling in the information related to the second resource: */
  cf_resource = &(cf_query_thermal->children[2]);
  cf_resource->key = "Resource";
  cf_resource->parent = cf_query_thermal;
  /***/
  cf_resource->values_num = 1;
  cf_resource->values =
      calloc(cf_resource->values_num, sizeof(*(cf_resource->values)));
  /***/
  if (cf_resource->values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_resource->values[0].type = OCONFIG_TYPE_STRING;
  cf_resource->values[0].value.string = "Temperatures";

  /* Allocating the property associated with the resource: */
  cf_resource->children_num = 1;
  cf_resource->children =
      calloc(cf_resource->children_num, sizeof(*(cf_resource->children)));
  /***/
  if (cf_resource->children == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property = &(cf_resource->children[0]);

  /* Filling in the information related to the property: */
  cf_property->key = "Property";
  cf_property->parent = cf_resource;
  /***/
  cf_property->values_num = 1;
  cf_property->values =
      calloc(cf_property->values_num, sizeof(*(cf_property->values)));
  /***/
  if (cf_property->values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->values[0].type = OCONFIG_TYPE_STRING;
  cf_property->values[0].value.string = "ReadingCelsius";

  /* Allocating the fields (i.e. children) of the property: */
  cf_property->children_num = 3;
  cf_property->children =
      calloc(cf_property->children_num, sizeof(*(cf_property->children)));
  /***/
  if (cf_property->children == NULL)
    return EXIT_FAILURE;

  /* Filling in the information related to the PluginInstance field of the
   * property: */
  cf_property->children[0].parent = cf_property;
  cf_property->children[0].key = "PluginInstance";
  /***/
  cf_property->children[0].values_num = 1;
  cf_property->children[0].values =
      calloc(cf_property->children[0].values_num,
             sizeof(*(cf_property->children[0].values)));
  /***/
  if (cf_property->children[0].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->children[0].values[0].type = OCONFIG_TYPE_STRING;
  cf_property->children[0].values[0].value.string = "Temperatures";

  /* Filling in the information related to the Type field of the property: */
  cf_property->children[1].parent = cf_property;
  cf_property->children[1].key = "Type";
  /***/
  cf_property->children[1].values_num = 1;
  cf_property->children[1].values =
      calloc(cf_property->children[1].values_num,
             sizeof(*(cf_property->children[1].values)));
  /***/
  if (cf_property->children[1].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->children[1].values[0].type = OCONFIG_TYPE_STRING;
  cf_property->children[1].values[0].value.string = "temperature";

  /* Filling in the information related to the SelectAttrValue field of the
   * property: */
  cf_property->children[2].parent = cf_property;
  cf_property->children[2].key = "SelectAttrValue";
  /***/
  cf_property->children[2].values_num = 2;
  cf_property->children[2].values =
      calloc(cf_property->children[2].values_num,
             sizeof(*(cf_property->children[2].values)));
  /***/
  if (cf_property->children[2].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->children[2].values[0].type = OCONFIG_TYPE_STRING;
  cf_property->children[2].values[0].value.string = "PhysicalContext";
  /***/
  cf_property->children[2].values[1].type = OCONFIG_TYPE_STRING;
  cf_property->children[2].values[1].value.string = "Intake";

  /**************************************************************************
   * Query "voltages" subpart:
   **************************************************************************/
  /* Allocating the root of the Query "voltages" subpart of the
   * configuration file: */
  cf_query_voltages->key = "Query";
  cf_query_voltages->parent = config_file;
  /***/
  cf_query_voltages->values_num = 1;
  cf_query_voltages->values = calloc(cf_query_voltages->values_num,
                                     sizeof(*(cf_query_voltages->values)));
  /***/
  if (cf_query_voltages->values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_query_voltages->values[0].type = OCONFIG_TYPE_STRING;
  cf_query_voltages->values[0].value.string = "voltages";

  /* Allocating the children of the Query "voltages" subpart: */
  cf_query_voltages->children_num = 2;
  cf_query_voltages->children = calloc(cf_query_voltages->children_num,
                                       sizeof(*(cf_query_voltages->children)));
  /***/
  if (cf_query_voltages->children == NULL)
    return EXIT_FAILURE;

  /* Filling in the information related to the endpoint: */
  cf_query_voltages->children[0].key = "Endpoint";
  cf_query_voltages->children[0].parent = cf_query_voltages;
  /***/
  cf_query_voltages->children[0].values_num = 1;
  cf_query_voltages->children[0].values =
      calloc(cf_query_voltages->children[0].values_num,
             sizeof(*(cf_query_voltages->children[0].values)));
  /***/
  if (cf_query_voltages->children[0].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_query_voltages->children[0].values[0].type = OCONFIG_TYPE_STRING;
  cf_query_voltages->children[0].values[0].value.string = "/Chassis[0]/Power";

  /* Filling in the information related to the resource: */
  cf_resource = &(cf_query_voltages->children[1]);
  cf_resource->key = "Resource";
  cf_resource->parent = cf_query_voltages;
  /***/
  cf_resource->values_num = 1;
  cf_resource->values =
      calloc(cf_resource->values_num, sizeof(*(cf_resource->values)));
  /***/
  if (cf_resource->values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_resource->values[0].type = OCONFIG_TYPE_STRING;
  cf_resource->values[0].value.string = "Voltages";

  /* Allocating the property associated with the resource: */
  cf_resource->children_num = 1;
  cf_resource->children =
      calloc(cf_resource->children_num, sizeof(*(cf_resource->children)));
  /***/
  if (cf_resource->children == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property = &(cf_resource->children[0]);

  /* Filling in the information related to the property: */
  cf_property->key = "Property";
  cf_property->parent = cf_resource;
  /***/
  cf_property->values_num = 1;
  cf_property->values =
      calloc(cf_property->values_num, sizeof(*(cf_property->values)));
  /***/
  if (cf_property->values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->values[0].type = OCONFIG_TYPE_STRING;
  cf_property->values[0].value.string = "ReadingVolts";

  /* Allocating the fields (i.e. children) of the property: */
  cf_property->children_num = 4;
  cf_property->children =
      calloc(cf_property->children_num, sizeof(*(cf_property->children)));
  /***/
  if (cf_property->children == NULL)
    return EXIT_FAILURE;

  /* Filling in the information related to the PluginInstance field of the
   * property: */
  cf_property->children[0].parent = cf_property;
  cf_property->children[0].key = "PluginInstance";
  /***/
  cf_property->children[0].values_num = 1;
  cf_property->children[0].values =
      calloc(cf_property->children[0].values_num,
             sizeof(*(cf_property->children[0].values)));
  /***/
  if (cf_property->children[0].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->children[0].values[0].type = OCONFIG_TYPE_STRING;
  cf_property->children[0].values[0].value.string = "Voltages";

  /* Filling in the information related to the Type field of the property: */
  cf_property->children[1].parent = cf_property;
  cf_property->children[1].key = "Type";
  /***/
  cf_property->children[1].values_num = 1;
  cf_property->children[1].values =
      calloc(cf_property->children[1].values_num,
             sizeof(*(cf_property->children[1].values)));
  /***/
  if (cf_property->children[1].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->children[1].values[0].type = OCONFIG_TYPE_STRING;
  cf_property->children[1].values[0].value.string = "voltage";

  /* Filling in the information related to the TypeInstance field of the
   * property: */
  cf_property->children[2].parent = cf_property;
  cf_property->children[2].key = "TypeInstance";
  /***/
  cf_property->children[2].values_num = 1;
  cf_property->children[2].values =
      calloc(cf_property->children[2].values_num,
             sizeof(*(cf_property->children[2].values)));
  /***/
  if (cf_property->children[2].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->children[2].values[0].type = OCONFIG_TYPE_STRING;
  cf_property->children[2].values[0].value.string = "VRM";

  /* Filling in the information related to the TypeInstance field of the
   * property: */
  cf_property->children[3].parent = cf_property;
  cf_property->children[3].key = "TypeInstancePrefixID";
  /***/
  cf_property->children[3].values_num = 1;
  cf_property->children[3].values =
      calloc(cf_property->children[3].values_num,
             sizeof(*(cf_property->children[3].values)));
  /***/
  if (cf_property->children[3].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->children[3].values[0].type = OCONFIG_TYPE_BOOLEAN;
  cf_property->children[3].values[0].value.boolean = true;

  /**************************************************************************
   * Query "temperatures" subpart:
   **************************************************************************/
  /* Allocating the root of the Query "temperatures" subpart of the
   * configuration file: */
  cf_query_temperatures->key = "Query";
  cf_query_temperatures->parent = config_file;
  /***/
  cf_query_temperatures->values_num = 1;
  cf_query_temperatures->values =
      calloc(cf_query_temperatures->values_num,
             sizeof(*(cf_query_temperatures->values)));
  /***/
  if (cf_query_temperatures->values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_query_temperatures->values[0].type = OCONFIG_TYPE_STRING;
  cf_query_temperatures->values[0].value.string = "temperatures";

  /* Allocating the children of the Query "temperatures" subpart: */
  cf_query_temperatures->children_num = 2;
  cf_query_temperatures->children =
      calloc(cf_query_temperatures->children_num,
             sizeof(*(cf_query_temperatures->children)));
  /***/
  if (cf_query_temperatures->children == NULL)
    return EXIT_FAILURE;

  /* Filling in the information related to the endpoint: */
  cf_query_temperatures->children[0].key = "Endpoint";
  cf_query_temperatures->children[0].parent = cf_query_temperatures;
  /***/
  cf_query_temperatures->children[0].values_num = 1;
  cf_query_temperatures->children[0].values =
      calloc(cf_query_temperatures->children[0].values_num,
             sizeof(*(cf_query_temperatures->children[0].values)));
  /***/
  if (cf_query_temperatures->children[0].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_query_temperatures->children[0].values[0].type = OCONFIG_TYPE_STRING;
  cf_query_temperatures->children[0].values[0].value.string =
      "/Chassis[0]/ThermalSubsystem/ThermalMetrics";

  /* Filling in the information related to the resource: */
  cf_resource = &(cf_query_temperatures->children[1]);
  cf_resource->key = "Resource";
  cf_resource->parent = cf_query_temperatures;
  /***/
  cf_resource->values_num = 1;
  cf_resource->values =
      calloc(cf_resource->values_num, sizeof(*(cf_resource->values)));
  /***/
  if (cf_resource->values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_resource->values[0].type = OCONFIG_TYPE_STRING;
  cf_resource->values[0].value.string = "TemperatureReadingsCelsius";

  /* Allocating the property associated with the resource: */
  cf_resource->children_num = 1;
  cf_resource->children =
      calloc(cf_resource->children_num, sizeof(*(cf_resource->children)));
  /***/
  if (cf_resource->children == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property = &(cf_resource->children[0]);

  /* Filling in the information related to the property: */
  cf_property->key = "Property";
  cf_property->parent = cf_resource;
  /***/
  cf_property->values_num = 1;
  cf_property->values =
      calloc(cf_property->values_num, sizeof(*(cf_property->values)));
  /***/
  if (cf_property->values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->values[0].type = OCONFIG_TYPE_STRING;
  cf_property->values[0].value.string = "Reading";

  /* Allocating the fields (i.e. children) of the property: */
  cf_property->children_num = 3;
  cf_property->children =
      calloc(cf_property->children_num, sizeof(*(cf_property->children)));
  /***/
  if (cf_property->children == NULL)
    return EXIT_FAILURE;

  /* Filling in the information related to the PluginInstance field of the
   * property: */
  cf_property->children[0].parent = cf_property;
  cf_property->children[0].key = "PluginInstance";
  /***/
  cf_property->children[0].values_num = 1;
  cf_property->children[0].values =
      calloc(cf_property->children[0].values_num,
             sizeof(*(cf_property->children[0].values)));
  /***/
  if (cf_property->children[0].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->children[0].values[0].type = OCONFIG_TYPE_STRING;
  cf_property->children[0].values[0].value.string = "Temperatures";

  /* Filling in the information related to the Type field of the property: */
  cf_property->children[1].parent = cf_property;
  cf_property->children[1].key = "Type";
  /***/
  cf_property->children[1].values_num = 1;
  cf_property->children[1].values =
      calloc(cf_property->children[1].values_num,
             sizeof(*(cf_property->children[1].values)));
  /***/
  if (cf_property->children[1].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->children[1].values[0].type = OCONFIG_TYPE_STRING;
  cf_property->children[1].values[0].value.string = "temperature";

  /* Filling in the information related to the TypeInstanceAttr field of the
   * property: */
  cf_property->children[2].parent = cf_property;
  cf_property->children[2].key = "TypeInstanceAttr";
  /***/
  cf_property->children[2].values_num = 1;
  cf_property->children[2].values =
      calloc(cf_property->children[2].values_num,
             sizeof(*(cf_property->children[2].values)));
  /***/
  if (cf_property->children[2].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->children[2].values[0].type = OCONFIG_TYPE_STRING;
  cf_property->children[2].values[0].value.string = "DeviceName";

  /**************************************************************************
   * Query "ps1_voltage" subpart:
   **************************************************************************/
  /* Allocating the root of the Query "ps1_voltage" subpart of the
   * configuration file: */
  cf_query_ps1_voltage->key = "Query";
  cf_query_ps1_voltage->parent = config_file;
  /***/
  cf_query_ps1_voltage->values_num = 1;
  cf_query_ps1_voltage->values =
      calloc(cf_query_ps1_voltage->values_num,
             sizeof(*(cf_query_ps1_voltage->values)));
  /***/
  if (cf_query_ps1_voltage->values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_query_ps1_voltage->values[0].type = OCONFIG_TYPE_STRING;
  cf_query_ps1_voltage->values[0].value.string = "ps1_voltage";

  /* Allocating the children of the Query "ps1_voltage" subpart: */
  cf_query_ps1_voltage->children_num = 2;
  cf_query_ps1_voltage->children =
      calloc(cf_query_ps1_voltage->children_num,
             sizeof(*(cf_query_ps1_voltage->children)));
  /***/
  if (cf_query_ps1_voltage->children == NULL)
    return EXIT_FAILURE;

  /* Filling in the information related to the endpoint: */
  cf_query_ps1_voltage->children[0].key = "Endpoint";
  cf_query_ps1_voltage->children[0].parent = cf_query_ps1_voltage;
  /***/
  cf_query_ps1_voltage->children[0].values_num = 1;
  cf_query_ps1_voltage->children[0].values =
      calloc(cf_query_ps1_voltage->children[0].values_num,
             sizeof(*(cf_query_ps1_voltage->children[0].values)));
  /***/
  if (cf_query_ps1_voltage->children[0].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_query_ps1_voltage->children[0].values[0].type = OCONFIG_TYPE_STRING;
  cf_query_ps1_voltage->children[0].values[0].value.string =
      "/Chassis[0]/Sensors[15]";

  /* Filling in the information related to the attribute: */
  cf_attribute = &(cf_query_ps1_voltage->children[1]);
  cf_attribute->key = "Attribute";
  cf_attribute->parent = cf_query_ps1_voltage;
  /***/
  cf_attribute->values_num = 1;
  cf_attribute->values =
      calloc(cf_attribute->values_num, sizeof(*(cf_attribute->values)));
  /***/
  if (cf_attribute->values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_attribute->values[0].type = OCONFIG_TYPE_STRING;
  cf_attribute->values[0].value.string = "Reading";

  /* Allocating the fields (i.e. children) of the attribute: */
  cf_attribute->children_num = 3;
  cf_attribute->children =
      calloc(cf_attribute->children_num, sizeof(*(cf_attribute->children)));
  /***/
  if (cf_attribute->children == NULL)
    return EXIT_FAILURE;

  /* Filling in the information related to the PluginInstance field of the
   * attribute: */
  cf_attribute->children[0].parent = cf_attribute;
  cf_attribute->children[0].key = "PluginInstance";
  /***/
  cf_attribute->children[0].values_num = 1;
  cf_attribute->children[0].values =
      calloc(cf_attribute->children[0].values_num,
             sizeof(*(cf_attribute->children[0].values)));
  /***/
  if (cf_attribute->children[0].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_attribute->children[0].values[0].type = OCONFIG_TYPE_STRING;
  cf_attribute->children[0].values[0].value.string = "Voltages";

  /* Filling in the information related to the Type field of the attribute: */
  cf_attribute->children[1].parent = cf_attribute;
  cf_attribute->children[1].key = "Type";
  /***/
  cf_attribute->children[1].values_num = 1;
  cf_attribute->children[1].values =
      calloc(cf_attribute->children[1].values_num,
             sizeof(*(cf_attribute->children[1].values)));
  /***/
  if (cf_attribute->children[1].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_attribute->children[1].values[0].type = OCONFIG_TYPE_STRING;
  cf_attribute->children[1].values[0].value.string = "voltage";

  /* Filling in the information related to the TypeInstance field of the
   * attribute: */
  cf_attribute->children[2].parent = cf_attribute;
  cf_attribute->children[2].key = "TypeInstance";
  /***/
  cf_attribute->children[2].values_num = 1;
  cf_attribute->children[2].values =
      calloc(cf_attribute->children[2].values_num,
             sizeof(*(cf_attribute->children[2].values)));
  /***/
  if (cf_attribute->children[2].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_attribute->children[2].values[0].type = OCONFIG_TYPE_STRING;
  cf_attribute->children[2].values[0].value.string = "PS1 Voltage";

  /**************************************************************************
   * Query "storage" subpart:
   **************************************************************************/
  /* Allocating the root of the Query "storage" subpart of the configuration
   * file: */
  cf_query_storage->key = "Query";
  cf_query_storage->parent = config_file;
  /***/
  cf_query_storage->values_num = 1;
  cf_query_storage->values =
      calloc(cf_query_storage->values_num, sizeof(*(cf_query_storage->values)));
  /***/
  if (cf_query_storage->values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_query_storage->values[0].type = OCONFIG_TYPE_STRING;
  cf_query_storage->values[0].value.string = "storage";

  /* Allocating the children of the Query "storage" subpart: */
  cf_query_storage->children_num = 2;
  cf_query_storage->children = calloc(cf_query_storage->children_num,
                                      sizeof(*(cf_query_storage->children)));
  /***/
  if (cf_query_storage->children == NULL)
    return EXIT_FAILURE;

  /* Filling in the information related to the endpoint: */
  cf_query_storage->children[0].key = "Endpoint";
  cf_query_storage->children[0].parent = cf_query_storage;
  /***/
  cf_query_storage->children[0].values_num = 1;
  cf_query_storage->children[0].values =
      calloc(cf_query_storage->children[0].values_num,
             sizeof(*(cf_query_storage->children[0].values)));
  /***/
  if (cf_query_storage->children[0].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_query_storage->children[0].values[0].type = OCONFIG_TYPE_STRING;
  cf_query_storage->children[0].values[0].value.string =
      "/Systems[0]/SimpleStorage[0]";

  /* Filling in the information related to the resource: */
  cf_resource = &(cf_query_storage->children[1]);
  cf_resource->key = "Resource";
  cf_resource->parent = cf_query_storage;
  /***/
  cf_resource->values_num = 1;
  cf_resource->values =
      calloc(cf_resource->values_num, sizeof(*(cf_resource->values)));
  /***/
  if (cf_resource->values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_resource->values[0].type = OCONFIG_TYPE_STRING;
  cf_resource->values[0].value.string = "Devices";

  /* Allocating the property associated with the resource: */
  cf_resource->children_num = 1;
  cf_resource->children =
      calloc(cf_resource->children_num, sizeof(*(cf_resource->children)));
  /***/
  if (cf_resource->children == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property = &(cf_resource->children[0]);

  /* Filling in the information related to the property: */
  cf_property->key = "Property";
  cf_property->parent = cf_resource;
  /***/
  cf_property->values_num = 1;
  cf_property->values =
      calloc(cf_property->values_num, sizeof(*(cf_property->values)));
  /***/
  if (cf_property->values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->values[0].type = OCONFIG_TYPE_STRING;
  cf_property->values[0].value.string = "CapacityBytes";

  /* Allocating the fields (i.e. children) of the property: */
  cf_property->children_num = 3;
  cf_property->children =
      calloc(cf_property->children_num, sizeof(*(cf_property->children)));
  /***/
  if (cf_property->children == NULL)
    return EXIT_FAILURE;

  /* Filling in the information related to the PluginInstance field of the
   * property: */
  cf_property->children[0].parent = cf_property;
  cf_property->children[0].key = "PluginInstance";
  /***/
  cf_property->children[0].values_num = 1;
  cf_property->children[0].values =
      calloc(cf_property->children[0].values_num,
             sizeof(*(cf_property->children[0].values)));
  /***/
  if (cf_property->children[0].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->children[0].values[0].type = OCONFIG_TYPE_STRING;
  cf_property->children[0].values[0].value.string = "Storage";

  /* Filling in the information related to the Type field of the property: */
  cf_property->children[1].parent = cf_property;
  cf_property->children[1].key = "Type";
  /***/
  cf_property->children[1].values_num = 1;
  cf_property->children[1].values =
      calloc(cf_property->children[1].values_num,
             sizeof(*(cf_property->children[1].values)));
  /***/
  if (cf_property->children[1].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->children[1].values[0].type = OCONFIG_TYPE_STRING;
  cf_property->children[1].values[0].value.string = "capacity";

  /* Filling in the information related to the SelectAttrs field of the
   * property: */
  cf_property->children[2].parent = cf_property;
  cf_property->children[2].key = "SelectAttrs";
  /***/
  cf_property->children[2].values_num = 2;
  cf_property->children[2].values =
      calloc(cf_property->children[2].values_num,
             sizeof(*(cf_property->children[2].values)));
  /***/
  if (cf_property->children[2].values == NULL)
    return EXIT_FAILURE;
  /***/
  cf_property->children[2].values[0].type = OCONFIG_TYPE_STRING;
  cf_property->children[2].values[0].value.string = "Model";
  /***/
  cf_property->children[2].values[1].type = OCONFIG_TYPE_STRING;
  cf_property->children[2].values[1].value.string = "Name";

  return EXIT_SUCCESS;
}

/*******/

static void
destroy_config_file_attribute(const oconfig_item_t *const cf_attribute) {
  /* Deallocating the attribute's values: */
  if (cf_attribute->values != NULL)
    free(cf_attribute->values);

  /* Deallocating the attribute's children/fields: */
  if (cf_attribute->children != NULL) {
    for (int i = 0; i < cf_attribute->children_num; i++) {
      if (cf_attribute->children[i].values != NULL) {
        free(cf_attribute->children[i].values);
      }
    }

    free(cf_attribute->children);
  }
}

/*******/

static void
destroy_config_file_property(const oconfig_item_t *const cf_property) {
  /* Deallocating the property's values: */
  if (cf_property->values != NULL)
    free(cf_property->values);

  /* Deallocating the property's children/fields: */
  if (cf_property->children != NULL) {
    for (int i = 0; i < cf_property->children_num; i++) {
      if (cf_property->children[i].values != NULL) {
        free(cf_property->children[i].values);
      }
    }

    free(cf_property->children);
  }
}

/*******/

static void
destroy_config_file_resource(const oconfig_item_t *const cf_resource) {
  /* Deallocating the resource's values: */
  if (cf_resource->values != NULL)
    free(cf_resource->values);

  /* Deallocating the resource's children, which are properties: */
  if (cf_resource->children != NULL) {
    for (int i = 0; i < cf_resource->children_num; i++) {
      destroy_config_file_property(&(cf_resource->children[i]));
    }

    free(cf_resource->children);
  }
}

/*******/

static void destroy_config_file_query(const oconfig_item_t *const cf_query) {
  /* Checking that the specified query should be deallocated: */
  if (cf_query != NULL) {
    /* Deallocating the values associated with the query: */
    if (cf_query->values != NULL)
      free(cf_query->values);

    /* Deallocating the children of the query, after individually
     * deallocating their content: */
    if (cf_query->children != NULL) {
      /* Deallocating all the children of the query: */
      for (int i = 0; i < cf_query->children_num; i++) {
        /* Deallocating the endpoint: */
        if (strcmp("Endpoint", cf_query->children[i].key) == 0) {
          if (cf_query->children[0].values != NULL) {
            free(cf_query->children[0].values);
          }
        }
        /* Deallocating the considered resource: */
        else if (strcmp("Resource", cf_query->children[i].key) == 0) {
          destroy_config_file_resource(&(cf_query->children[i]));
        }
        /* Deallocating the considered attribute: */
        else if (strcmp("Attribute", cf_query->children[i].key) == 0) {
          destroy_config_file_attribute(&(cf_query->children[i]));
        }
      }

      free(cf_query->children);
    }
  }
}

/*******/

/* Destroys the in-memory configuration file built by "build_config_file".
 * To do so, this function deallocates, if required, each sub-part (queries and
 * services) of the configuration file, before deallocating its root: */
static void destroy_config_file(void) {
  /**************************************************************************
   * Service:
   **************************************************************************/
  if (cf_service != NULL) {
    /* Deallocating the values associated with the Service: */
    if (cf_service->values != NULL)
      free(cf_service->values);

    /* Deallocating the children of the service, after individually
     * deallocating their content: */
    if (cf_service->children != NULL) {
      for (int i = 0; i < cf_service->children_num; i++) {
        if (cf_service->children[i].values != NULL) {
          free(cf_service->children[i].values);
        }
      }

      free(cf_service->children);
    }
  }

  /**************************************************************************
   * Queries:
   **************************************************************************/
  destroy_config_file_query(cf_query_thermal);
  destroy_config_file_query(cf_query_voltages);
  destroy_config_file_query(cf_query_temperatures);
  destroy_config_file_query(cf_query_ps1_voltage);
  destroy_config_file_query(cf_query_storage);

  /**************************************************************************
   * Root:
   **************************************************************************/
  if (config_file != NULL) {
    /* Deallocating the values associated with the root: */
    if (config_file->values != NULL)
      free(config_file->values);

    /* Deallocating the children associated with the root, that is to say
     * the service and queries, which content was already deallocated: */
    if (config_file->children != NULL)
      free(config_file->children);

    /* Deallocating the root itself: */
    free(config_file);
  }

  /**************************************************************************
   * Global reset:
   **************************************************************************/
  config_file = NULL;
  cf_service = NULL;
  cf_query_thermal = NULL;
  cf_query_voltages = NULL;
  cf_query_temperatures = NULL;
  cf_query_ps1_voltage = NULL;
  cf_query_storage = NULL;
}

/******************************************************************************
 * Tests:
 ******************************************************************************/
/* Conversion of parsed data types to collectd's data types: */
DEF_TEST(redfish_convert_val) {
  redfish_value_t val = {.string = "1"};
  redfish_value_type_t src_type = VAL_TYPE_STR;
  int dst_type = DS_TYPE_GAUGE;
  value_t vl = {0};
  int ret = redfish_convert_val(&val, src_type, &vl, dst_type);

  EXPECT_EQ_INT(0, ret);
  OK(vl.gauge == 1.0);

  val.integer = 1;
  src_type = VAL_TYPE_INT;
  dst_type = DS_TYPE_GAUGE;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.gauge == 1.0);

  val.real = 1.0;
  src_type = VAL_TYPE_REAL;
  dst_type = DS_TYPE_GAUGE;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.gauge == 1.0);

  val.string = "-1";
  src_type = VAL_TYPE_STR;
  dst_type = DS_TYPE_DERIVE;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.derive == -1);

  val.integer = -1;
  src_type = VAL_TYPE_INT;
  dst_type = DS_TYPE_DERIVE;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.derive == -1);

  val.real = -1.0;
  src_type = VAL_TYPE_REAL;
  dst_type = DS_TYPE_DERIVE;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.derive == -1);

  val.string = "1";
  src_type = VAL_TYPE_STR;
  dst_type = DS_TYPE_COUNTER;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.counter == 1);

  val.integer = 1;
  src_type = VAL_TYPE_INT;
  dst_type = DS_TYPE_COUNTER;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.counter == 1);

  val.real = 1.0;
  src_type = VAL_TYPE_REAL;
  dst_type = DS_TYPE_COUNTER;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.counter == 1);

  val.string = "1";
  src_type = VAL_TYPE_STR;
  dst_type = DS_TYPE_ABSOLUTE;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.absolute == 1);

  val.integer = 1;
  src_type = VAL_TYPE_INT;
  dst_type = DS_TYPE_ABSOLUTE;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.absolute == 1);

  val.real = 1.0;
  src_type = VAL_TYPE_REAL;
  dst_type = DS_TYPE_ABSOLUTE;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.absolute == 1);

  return 0;
}

/*******/

/* Testing the memory allocation for the context structure.
 * Creation of services list & queries AVL tree: */
DEF_TEST(redfish_preconfig) {
  int ret = redfish_preconfig();

  EXPECT_EQ_INT(0, ret);
  CHECK_NOT_NULL(ctx.queries);
  CHECK_NOT_NULL(ctx.services);

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(redfish_config) {
  redfish_query_t *query = NULL;

  int ret = redfish_config(config_file);

  EXPECT_EQ_INT(0, ret);

  EXPECT_EQ_INT(1, llist_size(ctx.services));
  EXPECT_EQ_STR("mock1U", llist_head(ctx.services)->key);

  EXPECT_EQ_INT(5, c_avl_size(ctx.queries));
  /***/
  c_avl_get(ctx.queries, "thermal", (void **)(&query));
  EXPECT_EQ_STR("thermal", query->name);
  /***/
  c_avl_get(ctx.queries, "voltages", (void **)(&query));
  EXPECT_EQ_STR("voltages", query->name);
  /***/
  c_avl_get(ctx.queries, "temperatures", (void **)(&query));
  EXPECT_EQ_STR("temperatures", query->name);
  /***/
  c_avl_get(ctx.queries, "ps1_voltage", (void **)(&query));
  EXPECT_EQ_STR("ps1_voltage", query->name);
  /***/
  c_avl_get(ctx.queries, "storage", (void **)(&query));
  EXPECT_EQ_STR("storage", query->name);

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(redfish_config_service) {

  int ret = redfish_preconfig();
  EXPECT_EQ_INT(0, ret);

  ret = redfish_config_service(cf_service);
  EXPECT_EQ_INT(0, ret);
  /***/
  redfish_service_t *service = llist_head(ctx.services)->value;
  /***/
  EXPECT_EQ_STR("mock1U", service->name);
  EXPECT_EQ_STR("localhost:10000", service->host);
  EXPECT_EQ_STR("", service->user);
  EXPECT_EQ_STR("", service->passwd);
  EXPECT_EQ_PTR(NULL, service->token);
  EXPECT_EQ_INT(5, service->queries_num);
  CHECK_NOT_NULL(service->queries);
  EXPECT_EQ_PTR(NULL, service->query_ptrs);

  redfish_cleanup();

  return 0;
}

/*******/

/* Reading the names of the queries from the configuration files: */
DEF_TEST(redfish_read_queries) {
  char **queries = NULL;
  int ret = redfish_read_queries(&(cf_service->children[3]), &queries);

  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_STR("thermal", queries[0]);
  EXPECT_EQ_STR("voltages", queries[1]);
  EXPECT_EQ_STR("temperatures", queries[2]);
  EXPECT_EQ_STR("ps1_voltage", queries[3]);
  EXPECT_EQ_STR("storage", queries[4]);

  for (int j = 0; j < cf_service->children[3].values_num; j++) {
    sfree(queries[j]);
  }
  /***/
  sfree(queries);

  return 0;
}

/*******/

static redfish_query_t *
redfish_config_get_query_struct(oconfig_item_t *cf_query,
                                const char *const query_name) {
  /* Initialisation of the global data structures: */
  redfish_preconfig();

  /* Populating the data structure associated with the specified query.
   * The latter will be inserted into an AVL tree: */
  redfish_config_query(cf_query, ctx.queries);

  /* Extracting the data structure representing the query from the AVL
   * tree: */
  redfish_query_t *query = NULL;
  /***/
  c_avl_get(ctx.queries, query_name, (void **)(&query));

  /* Returning the data structure associated with the query: */
  return query;
}

/*******/

DEF_TEST(redfish_config_query_thermal) {
  int ret = redfish_preconfig();
  EXPECT_EQ_INT(0, ret);

  ret = redfish_config_query(cf_query_thermal, ctx.queries);
  EXPECT_EQ_INT(0, ret);

  redfish_query_t *query = NULL;
  /***/
  ret = c_avl_get(ctx.queries, "thermal", (void **)(&query));
  EXPECT_EQ_INT(0, ret);
  CHECK_NOT_NULL(query);
  /***/
  EXPECT_EQ_STR("thermal", query->name);
  EXPECT_EQ_STR("/Chassis[0]/Thermal", query->endpoint);
  CHECK_NOT_NULL(query->resources);
  EXPECT_EQ_UINT64(2, llist_size(query->resources));
  CHECK_NOT_NULL(query->attributes);
  EXPECT_EQ_UINT64(0, llist_size(query->attributes));

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(redfish_config_resource_thermal_fans) {
  redfish_query_t *query =
      redfish_config_get_query_struct(cf_query_thermal, "thermal");
  /***/
  CHECK_NOT_NULL(query);

  redfish_resource_t *fans = llist_head(query->resources)->value;
  /***/
  CHECK_NOT_NULL(fans);
  EXPECT_EQ_STR("Fans", fans->name);
  CHECK_NOT_NULL(fans->properties);

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(redfish_config_property_thermal_fans_reading) {
  redfish_query_t *query =
      redfish_config_get_query_struct(cf_query_thermal, "thermal");
  /***/
  CHECK_NOT_NULL(query);

  redfish_resource_t *fans = llist_head(query->resources)->value;
  /***/
  CHECK_NOT_NULL(fans);

  redfish_property_t *reading = llist_head(fans->properties)->value;
  /***/
  CHECK_NOT_NULL(reading);
  EXPECT_EQ_STR("Reading", reading->name);
  EXPECT_EQ_STR("Fans", reading->plugin_inst);
  EXPECT_EQ_STR("fanspeed", reading->type);
  EXPECT_EQ_PTR(NULL, reading->type_inst);
  EXPECT_EQ_STR("Name", reading->type_inst_attr);
  OK(!reading->type_inst_prefix_id);
  EXPECT_EQ_UINT64(1, reading->nb_select_ids);
  /***/
  CHECK_NOT_NULL(reading->select_ids);
  EXPECT_EQ_UINT64(1, reading->select_ids[0]);
  /***/
  EXPECT_EQ_UINT64(0, reading->nb_select_attrs);
  EXPECT_EQ_PTR(NULL, reading->select_attrs);
  EXPECT_EQ_PTR(NULL, reading->select_attrvalues);

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(redfish_config_resource_thermal_temperatures) {
  redfish_query_t *query =
      redfish_config_get_query_struct(cf_query_thermal, "thermal");
  /***/
  CHECK_NOT_NULL(query);

  redfish_resource_t *temperatures = llist_head(query->resources)->next->value;
  /***/
  CHECK_NOT_NULL(temperatures);
  EXPECT_EQ_STR("Temperatures", temperatures->name);
  CHECK_NOT_NULL(temperatures->properties);

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(redfish_config_property_thermal_temperatures_readingcelsius) {
  redfish_query_t *query =
      redfish_config_get_query_struct(cf_query_thermal, "thermal");
  /***/
  CHECK_NOT_NULL(query);

  redfish_resource_t *temperatures = llist_head(query->resources)->next->value;
  /***/
  CHECK_NOT_NULL(temperatures);

  redfish_property_t *reading_celsius =
      llist_head(temperatures->properties)->value;
  /***/
  CHECK_NOT_NULL(reading_celsius);
  EXPECT_EQ_STR("ReadingCelsius", reading_celsius->name);
  EXPECT_EQ_STR("Temperatures", reading_celsius->plugin_inst);
  EXPECT_EQ_STR("temperature", reading_celsius->type);
  EXPECT_EQ_PTR(NULL, reading_celsius->type_inst);
  EXPECT_EQ_PTR(NULL, reading_celsius->type_inst_attr);
  OK(!reading_celsius->type_inst_prefix_id);
  EXPECT_EQ_UINT64(0, reading_celsius->nb_select_ids);
  EXPECT_EQ_PTR(NULL, reading_celsius->select_ids);
  EXPECT_EQ_UINT64(0, reading_celsius->nb_select_attrs);
  EXPECT_EQ_PTR(NULL, reading_celsius->select_attrs);

  CHECK_NOT_NULL(reading_celsius->select_attrvalues);
  /***/
  llentry_t *le = llist_head(reading_celsius->select_attrvalues);
  /***/
  EXPECT_EQ_STR("PhysicalContext", le->key);
  EXPECT_EQ_STR("Intake", le->value);

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(redfish_config_query_voltages) {
  int ret = redfish_preconfig();
  EXPECT_EQ_INT(0, ret);

  ret = redfish_config_query(cf_query_voltages, ctx.queries);
  EXPECT_EQ_INT(0, ret);

  redfish_query_t *query = NULL;
  /***/
  ret = c_avl_get(ctx.queries, "voltages", (void **)(&query));
  EXPECT_EQ_INT(0, ret);
  CHECK_NOT_NULL(query);
  /***/
  EXPECT_EQ_STR("voltages", query->name);
  EXPECT_EQ_STR("/Chassis[0]/Power", query->endpoint);
  CHECK_NOT_NULL(query->resources);
  EXPECT_EQ_UINT64(1, llist_size(query->resources));
  CHECK_NOT_NULL(query->attributes);
  EXPECT_EQ_UINT64(0, llist_size(query->attributes));

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(redfish_config_resource_voltages_voltages) {
  redfish_query_t *query =
      redfish_config_get_query_struct(cf_query_voltages, "voltages");
  /***/
  CHECK_NOT_NULL(query);

  redfish_resource_t *voltages = llist_head(query->resources)->value;
  /***/
  CHECK_NOT_NULL(voltages);
  EXPECT_EQ_STR("Voltages", voltages->name);
  CHECK_NOT_NULL(voltages->properties);

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(redfish_config_property_voltages_voltages_readingvolts) {
  redfish_query_t *query =
      redfish_config_get_query_struct(cf_query_voltages, "voltages");
  /***/
  CHECK_NOT_NULL(query);

  redfish_resource_t *voltages = llist_head(query->resources)->value;
  /***/
  CHECK_NOT_NULL(voltages);

  redfish_property_t *reading_volts = llist_head(voltages->properties)->value;
  /***/
  CHECK_NOT_NULL(reading_volts);
  EXPECT_EQ_STR("ReadingVolts", reading_volts->name);
  EXPECT_EQ_STR("Voltages", reading_volts->plugin_inst);
  EXPECT_EQ_STR("voltage", reading_volts->type);
  EXPECT_EQ_STR("VRM", reading_volts->type_inst);
  EXPECT_EQ_PTR(NULL, reading_volts->type_inst_attr);
  OK(reading_volts->type_inst_prefix_id);
  EXPECT_EQ_UINT64(0, reading_volts->nb_select_ids);
  EXPECT_EQ_PTR(NULL, reading_volts->select_ids);
  EXPECT_EQ_UINT64(0, reading_volts->nb_select_attrs);
  EXPECT_EQ_PTR(NULL, reading_volts->select_attrs);
  EXPECT_EQ_PTR(NULL, reading_volts->select_attrvalues);

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(redfish_config_query_temperatures) {
  int ret = redfish_preconfig();
  EXPECT_EQ_INT(0, ret);

  ret = redfish_config_query(cf_query_temperatures, ctx.queries);
  EXPECT_EQ_INT(0, ret);

  redfish_query_t *query = NULL;
  /***/
  ret = c_avl_get(ctx.queries, "temperatures", (void **)(&query));
  EXPECT_EQ_INT(0, ret);
  CHECK_NOT_NULL(query);
  /***/
  EXPECT_EQ_STR("temperatures", query->name);
  EXPECT_EQ_STR("/Chassis[0]/ThermalSubsystem/ThermalMetrics", query->endpoint);
  CHECK_NOT_NULL(query->resources);
  EXPECT_EQ_UINT64(1, llist_size(query->resources));
  CHECK_NOT_NULL(query->attributes);
  EXPECT_EQ_UINT64(0, llist_size(query->attributes));

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(redfish_config_resource_temperatures_trc) {
  redfish_query_t *query =
      redfish_config_get_query_struct(cf_query_temperatures, "temperatures");
  /***/
  CHECK_NOT_NULL(query);

  redfish_resource_t *trc = llist_head(query->resources)->value;
  /***/
  CHECK_NOT_NULL(trc);
  EXPECT_EQ_STR("TemperatureReadingsCelsius", trc->name);
  CHECK_NOT_NULL(trc->properties);

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(redfish_config_property_temperatures_trc_reading) {
  redfish_query_t *query =
      redfish_config_get_query_struct(cf_query_temperatures, "temperatures");
  /***/
  CHECK_NOT_NULL(query);

  redfish_resource_t *trc = llist_head(query->resources)->value;
  /***/
  CHECK_NOT_NULL(trc);

  redfish_property_t *reading = llist_head(trc->properties)->value;
  /***/
  CHECK_NOT_NULL(reading);
  EXPECT_EQ_STR("Reading", reading->name);
  EXPECT_EQ_STR("Temperatures", reading->plugin_inst);
  EXPECT_EQ_STR("temperature", reading->type);
  EXPECT_EQ_PTR(NULL, reading->type_inst);
  EXPECT_EQ_STR("DeviceName", reading->type_inst_attr);
  OK(!reading->type_inst_prefix_id);
  EXPECT_EQ_UINT64(0, reading->nb_select_ids);
  EXPECT_EQ_PTR(NULL, reading->select_ids);
  EXPECT_EQ_UINT64(0, reading->nb_select_attrs);
  EXPECT_EQ_PTR(NULL, reading->select_attrs);
  EXPECT_EQ_PTR(NULL, reading->select_attrvalues);

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(redfish_config_query_ps1_voltage) {
  int ret = redfish_preconfig();
  EXPECT_EQ_INT(0, ret);

  ret = redfish_config_query(cf_query_ps1_voltage, ctx.queries);
  EXPECT_EQ_INT(0, ret);

  redfish_query_t *query = NULL;
  /***/
  ret = c_avl_get(ctx.queries, "ps1_voltage", (void **)(&query));
  EXPECT_EQ_INT(0, ret);
  CHECK_NOT_NULL(query);
  /***/
  EXPECT_EQ_STR("ps1_voltage", query->name);
  EXPECT_EQ_STR("/Chassis[0]/Sensors[15]", query->endpoint);
  CHECK_NOT_NULL(query->resources);
  EXPECT_EQ_UINT64(0, llist_size(query->resources));
  CHECK_NOT_NULL(query->attributes);
  EXPECT_EQ_UINT64(1, llist_size(query->attributes));

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(redfish_config_attribute_ps1_voltage_reading) {
  redfish_query_t *query =
      redfish_config_get_query_struct(cf_query_ps1_voltage, "ps1_voltage");
  /***/
  CHECK_NOT_NULL(query);

  redfish_attribute_t *reading = llist_head(query->attributes)->value;
  /***/
  CHECK_NOT_NULL(reading);
  EXPECT_EQ_STR("Reading", reading->name);
  EXPECT_EQ_STR("Voltages", reading->plugin_inst);
  EXPECT_EQ_STR("voltage", reading->type);
  EXPECT_EQ_STR("PS1 Voltage", reading->type_inst);

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(redfish_config_query_storage) {
  int ret = redfish_preconfig();
  EXPECT_EQ_INT(0, ret);

  ret = redfish_config_query(cf_query_storage, ctx.queries);
  EXPECT_EQ_INT(0, ret);

  redfish_query_t *query = NULL;
  /***/
  ret = c_avl_get(ctx.queries, "storage", (void **)(&query));
  EXPECT_EQ_INT(0, ret);
  CHECK_NOT_NULL(query);
  /***/
  EXPECT_EQ_STR("storage", query->name);
  EXPECT_EQ_STR("/Systems[0]/SimpleStorage[0]", query->endpoint);
  CHECK_NOT_NULL(query->resources);
  EXPECT_EQ_UINT64(1, llist_size(query->resources));
  CHECK_NOT_NULL(query->attributes);
  EXPECT_EQ_UINT64(0, llist_size(query->attributes));

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(redfish_config_resource_storage_devices) {
  redfish_query_t *query =
      redfish_config_get_query_struct(cf_query_storage, "storage");
  /***/
  CHECK_NOT_NULL(query);

  redfish_resource_t *devices = llist_head(query->resources)->value;
  /***/
  CHECK_NOT_NULL(devices);
  EXPECT_EQ_STR("Devices", devices->name);
  CHECK_NOT_NULL(devices->properties);

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(redfish_config_property_storage_devices_capacitybytes) {
  redfish_query_t *query =
      redfish_config_get_query_struct(cf_query_storage, "storage");
  /***/
  CHECK_NOT_NULL(query);

  redfish_resource_t *devices = llist_head(query->resources)->value;
  /***/
  CHECK_NOT_NULL(devices);

  redfish_property_t *capacity_bytes = llist_head(devices->properties)->value;
  /***/
  CHECK_NOT_NULL(capacity_bytes);
  EXPECT_EQ_STR("CapacityBytes", capacity_bytes->name);
  EXPECT_EQ_STR("Storage", capacity_bytes->plugin_inst);
  EXPECT_EQ_STR("capacity", capacity_bytes->type);
  EXPECT_EQ_PTR(NULL, capacity_bytes->type_inst);
  EXPECT_EQ_PTR(NULL, capacity_bytes->type_inst_attr);
  OK(!capacity_bytes->type_inst_prefix_id);
  EXPECT_EQ_UINT64(0, capacity_bytes->nb_select_ids);
  EXPECT_EQ_PTR(NULL, capacity_bytes->select_ids);
  /***/
  EXPECT_EQ_UINT64(2, capacity_bytes->nb_select_attrs);
  CHECK_NOT_NULL(capacity_bytes->select_attrs);
  EXPECT_EQ_STR("Model", capacity_bytes->select_attrs[0]);
  EXPECT_EQ_STR("Name", capacity_bytes->select_attrs[1]);
  /***/
  EXPECT_EQ_PTR(NULL, capacity_bytes->select_attrvalues);

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(process_payload_query_thermal) {
  int ret = redfish_config(config_file);
  /***/
  EXPECT_EQ_INT(0, ret);

  redfish_service_t *service = llist_head(ctx.services)->value;
  /***/
  redfish_query_t *query = NULL;
  c_avl_get(ctx.queries, "thermal", (void **)(&query));
  /***/
  redfish_payload_ctx_t payload = {.service = service, .query = query};
  /***/
  redfish_job_t job = {.next = NULL, .prev = NULL, .service_query = &payload};

  json_error_t error;
  json_t *root = json_loads(json_payloads[0], 0, &error);
  /***/
  if (!root)
    return -1;

  redfishPayload redfish_payload = {.json = root};

  redfish_process_payload(true, 200, &redfish_payload, &job);

  json_decref(root);

  value_list_t *v = redfish_test_get_next_dispatched_values();
  /***/
  EXPECT_EQ_INT(1, v->values_len);
  /***/
  EXPECT_EQ_STR("redfish", v->plugin);
  EXPECT_EQ_STR("mock1U", v->host);
  EXPECT_EQ_STR("Fans", v->plugin_instance);
  EXPECT_EQ_STR("fanspeed", v->type);
  EXPECT_EQ_STR("BaseBoard System Fan Backup", v->type_instance);
  EXPECT_EQ_DOUBLE(2050, v->values[0].gauge);
  /***/
  redfish_test_remove_next_dispatched_values();

  v = redfish_test_get_next_dispatched_values();
  /***/
  EXPECT_EQ_INT(1, v->values_len);
  /***/
  EXPECT_EQ_STR("redfish", v->plugin);
  EXPECT_EQ_STR("mock1U", v->host);
  EXPECT_EQ_STR("Temperatures", v->plugin_instance);
  EXPECT_EQ_STR("temperature", v->type);
  EXPECT_EQ_STR("Chassis Intake Temp", v->type_instance);
  EXPECT_EQ_DOUBLE(25, v->values[0].gauge);
  /***/
  redfish_test_remove_next_dispatched_values();

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(process_payload_query_voltages) {
  int ret = redfish_config(config_file);
  /***/
  EXPECT_EQ_INT(0, ret);

  redfish_service_t *service = llist_head(ctx.services)->value;
  /***/
  redfish_query_t *query = NULL;
  c_avl_get(ctx.queries, "voltages", (void **)(&query));
  /***/
  redfish_payload_ctx_t payload = {.service = service, .query = query};
  /***/
  redfish_job_t job = {.next = NULL, .prev = NULL, .service_query = &payload};

  json_error_t error;
  json_t *root = json_loads(json_payloads[1], 0, &error);
  /***/
  if (!root)
    return -1;

  redfishPayload redfish_payload = {.json = root};

  redfish_process_payload(true, 200, &redfish_payload, &job);

  json_decref(root);

  value_list_t *v = redfish_test_get_next_dispatched_values();
  /***/
  EXPECT_EQ_INT(1, v->values_len);
  /***/
  EXPECT_EQ_STR("redfish", v->plugin);
  EXPECT_EQ_STR("mock1U", v->host);
  EXPECT_EQ_STR("Voltages", v->plugin_instance);
  EXPECT_EQ_STR("voltage", v->type);
  EXPECT_EQ_STR("0-VRM", v->type_instance);
  EXPECT_EQ_DOUBLE(12, v->values[0].gauge);
  /***/
  redfish_test_remove_next_dispatched_values();

  v = redfish_test_get_next_dispatched_values();
  /***/
  EXPECT_EQ_INT(1, v->values_len);
  /***/
  EXPECT_EQ_STR("redfish", v->plugin);
  EXPECT_EQ_STR("mock1U", v->host);
  EXPECT_EQ_STR("Voltages", v->plugin_instance);
  EXPECT_EQ_STR("voltage", v->type);
  EXPECT_EQ_STR("1-VRM", v->type_instance);
  EXPECT_EQ_DOUBLE(5, v->values[0].gauge);
  /***/
  redfish_test_remove_next_dispatched_values();

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(process_payload_query_temperatures) {
  int ret = redfish_config(config_file);
  /***/
  EXPECT_EQ_INT(0, ret);

  redfish_service_t *service = llist_head(ctx.services)->value;
  /***/
  redfish_query_t *query = NULL;
  c_avl_get(ctx.queries, "temperatures", (void **)(&query));
  /***/
  redfish_payload_ctx_t payload = {.service = service, .query = query};
  /***/
  redfish_job_t job = {.next = NULL, .prev = NULL, .service_query = &payload};

  json_error_t error;
  json_t *root = json_loads(json_payloads[2], 0, &error);
  /***/
  if (!root)
    return -1;

  redfishPayload redfish_payload = {.json = root};

  redfish_process_payload(true, 200, &redfish_payload, &job);

  json_decref(root);

  value_list_t *v = redfish_test_get_next_dispatched_values();
  /***/
  EXPECT_EQ_INT(1, v->values_len);
  /***/
  EXPECT_EQ_STR("redfish", v->plugin);
  EXPECT_EQ_STR("mock1U", v->host);
  EXPECT_EQ_STR("Temperatures", v->plugin_instance);
  EXPECT_EQ_STR("temperature", v->type);
  EXPECT_EQ_STR("Intake", v->type_instance);
  EXPECT_EQ_DOUBLE(24.8, v->values[0].gauge);
  /***/
  redfish_test_remove_next_dispatched_values();

  v = redfish_test_get_next_dispatched_values();
  /***/
  EXPECT_EQ_INT(1, v->values_len);
  /***/
  EXPECT_EQ_STR("redfish", v->plugin);
  EXPECT_EQ_STR("mock1U", v->host);
  EXPECT_EQ_STR("Temperatures", v->plugin_instance);
  EXPECT_EQ_STR("temperature", v->type);
  EXPECT_EQ_STR("Exhaust", v->type_instance);
  EXPECT_EQ_DOUBLE(40.5, v->values[0].gauge);
  /***/
  redfish_test_remove_next_dispatched_values();

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(process_payload_query_ps1_voltage) {
  int ret = redfish_config(config_file);
  /***/
  EXPECT_EQ_INT(0, ret);

  redfish_service_t *service = llist_head(ctx.services)->value;
  /***/
  redfish_query_t *query = NULL;
  c_avl_get(ctx.queries, "ps1_voltage", (void **)(&query));
  /***/
  redfish_payload_ctx_t payload = {.service = service, .query = query};
  /***/
  redfish_job_t job = {.next = NULL, .prev = NULL, .service_query = &payload};

  json_error_t error;
  json_t *root = json_loads(json_payloads[3], 0, &error);
  /***/
  if (!root)
    return -1;

  redfishPayload redfish_payload = {.json = root};

  redfish_process_payload(true, 200, &redfish_payload, &job);

  json_decref(root);

  value_list_t *v = redfish_test_get_next_dispatched_values();
  /***/
  EXPECT_EQ_INT(1, v->values_len);
  /***/
  EXPECT_EQ_STR("redfish", v->plugin);
  EXPECT_EQ_STR("mock1U", v->host);
  EXPECT_EQ_STR("Voltages", v->plugin_instance);
  EXPECT_EQ_STR("voltage", v->type);
  EXPECT_EQ_STR("PS1 Voltage", v->type_instance);
  EXPECT_EQ_DOUBLE(119.27, v->values[0].gauge);
  /***/
  redfish_test_remove_next_dispatched_values();

  redfish_cleanup();

  return 0;
}

/*******/

DEF_TEST(process_payload_query_storage) {
  int ret = redfish_config(config_file);
  /***/
  EXPECT_EQ_INT(0, ret);

  redfish_service_t *service = llist_head(ctx.services)->value;
  /***/
  redfish_query_t *query = NULL;
  c_avl_get(ctx.queries, "storage", (void **)(&query));
  /***/
  redfish_payload_ctx_t payload = {.service = service, .query = query};
  /***/
  redfish_job_t job = {.next = NULL, .prev = NULL, .service_query = &payload};

  json_error_t error;
  json_t *root = json_loads(json_payloads[4], 0, &error);
  /***/
  if (!root)
    return -1;

  redfishPayload redfish_payload = {.json = root};

  redfish_process_payload(true, 200, &redfish_payload, &job);

  json_decref(root);

  value_list_t *v = redfish_test_get_next_dispatched_values();
  /***/
  EXPECT_EQ_INT(1, v->values_len);
  /***/
  EXPECT_EQ_STR("redfish", v->plugin);
  EXPECT_EQ_STR("mock1U", v->host);
  EXPECT_EQ_STR("Storage", v->plugin_instance);
  EXPECT_EQ_STR("capacity", v->type);
  EXPECT_EQ_STR("SATA Bay 1", v->type_instance);
  EXPECT_EQ_DOUBLE(8000000000000, v->values[0].gauge);
  /***/
  redfish_test_remove_next_dispatched_values();

  v = redfish_test_get_next_dispatched_values();
  /***/
  EXPECT_EQ_INT(1, v->values_len);
  /***/
  EXPECT_EQ_STR("redfish", v->plugin);
  EXPECT_EQ_STR("mock1U", v->host);
  EXPECT_EQ_STR("Storage", v->plugin_instance);
  EXPECT_EQ_STR("capacity", v->type);
  EXPECT_EQ_STR("SATA Bay 2", v->type_instance);
  EXPECT_EQ_DOUBLE(4000000000000, v->values[0].gauge);
  /***/
  redfish_test_remove_next_dispatched_values();

  redfish_cleanup();

  return 0;
}

/******************************************************************************
 * Main:
 ******************************************************************************/
int main(void) {
  /* Initialisation of the list of dispatched values: */
  last_dispatched_values_list = llist_create();
  /***/
  if (last_dispatched_values_list == NULL)
    return EXIT_FAILURE;

  /* Building the in-memory version of the configuration file: */
  if (EXIT_FAILURE == build_config_file()) {
    destroy_config_file();
    return EXIT_FAILURE;
  }

#if defined(COLLECT_DEBUG) && defined(REDFISH_TEST_PRINT_CONFIG)
  /* If the dedicated preprocessing variable was defined, printing the
   * configuration tree, notably for debug purposes: */
  oconfig_print_tree(config_file, OCONFIG_PRINT_TREE_INDENT_MAX_LVL,
                     OCONFIG_PRINT_TREE_INDENT_IN_SPACES, stderr);
#endif

  /* Running the tests: */
  RUN_TEST(redfish_convert_val);
  RUN_TEST(redfish_preconfig);
  RUN_TEST(redfish_config);
  RUN_TEST(redfish_config_service);
  RUN_TEST(redfish_read_queries);
  RUN_TEST(redfish_config_query_thermal);
  RUN_TEST(redfish_config_resource_thermal_fans);
  RUN_TEST(redfish_config_property_thermal_fans_reading);
  RUN_TEST(redfish_config_resource_thermal_temperatures);
  RUN_TEST(redfish_config_property_thermal_temperatures_readingcelsius);
  RUN_TEST(redfish_config_query_voltages);
  RUN_TEST(redfish_config_resource_voltages_voltages);
  RUN_TEST(redfish_config_property_voltages_voltages_readingvolts);
  RUN_TEST(redfish_config_query_temperatures);
  RUN_TEST(redfish_config_resource_temperatures_trc);
  RUN_TEST(redfish_config_property_temperatures_trc_reading);
  RUN_TEST(redfish_config_query_ps1_voltage);
  RUN_TEST(redfish_config_attribute_ps1_voltage_reading);
  RUN_TEST(redfish_config_query_storage);
  RUN_TEST(redfish_config_resource_storage_devices);
  RUN_TEST(redfish_config_property_storage_devices_capacitybytes);
  RUN_TEST(process_payload_query_thermal);
  RUN_TEST(process_payload_query_voltages);
  RUN_TEST(process_payload_query_temperatures);
  RUN_TEST(process_payload_query_ps1_voltage);
  RUN_TEST(process_payload_query_storage);

  /* Destroying the in-memory version of the configuration file: */
  destroy_config_file();

  /* Destroying the list of dispatched values: */
  for (llentry_t *le = llist_head(last_dispatched_values_list); le != NULL;
       le = le->next) {
    value_list_t *vl = le->value;
    /***/
    if (vl != NULL) {
      if (vl->values != NULL)
        free(vl->values);
      free(vl);
      le->value = NULL;
    }
  }
  /***/
  llist_destroy(last_dispatched_values_list);

  /* Termination and summary of the test suite: */
  END_TEST;
}
