/**
 * collectd - src/ras.c
 * MIT License
 *
 * Copyright (C) 2020  Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Bartlomiej Kotlowski <bartlomiej.kotlowski@intel.com>
 **/

#include "collectd.h"
#include "daemon/plugin.h"
#include "utils/common/common.h"

#include <sqlite3.h>

#define RAS_PLUGIN "ras"
#define DEFAULT_DB_PATH "/var/lib/rasdaemon/ras-mc_event.db"
#define SQL_QUERY_BUFFER_SIZE 128
#define PLUGIN_INST_SIZE 30
#define RAS_TYPE "ras_errors"
#define STRTOL_ERROR_RET_VAL 0

struct ras_metrics_per_CPU {
  unsigned long long ras_unclassified_mce_errors;
  unsigned long long ras_microcode_rom_parity_errors;
  unsigned long long ras_external_mce_errors;
  unsigned long long ras_frc_errors;
  unsigned long long ras_internal_parity_error;
  unsigned long long ras_smm_handler_code_access_violation_errors;
  unsigned long long ras_internal_timer_errors;
  unsigned long long ras_processor_bus_errors;
  unsigned long long ras_processor_base_errors;
  unsigned long long ras_memory_read_corrected_errors;
  unsigned long long ras_memory_write_corrected_errors;
  unsigned long long ras_memory_read_uncorrectable_errors;
  unsigned long long ras_memory_write_uncorrectable_errors;
  unsigned long long ras_cache_l0_l1_errors;
  unsigned long long ras_tlb_instruction_errors;
};

struct ras_metrics_per_server {
  unsigned long long ras_cache_l2_errors;
  unsigned long long ras_upi_errors;
  struct ras_metrics_per_CPU *per_CPU;
} static ras_metrics_server;

static const char *config_keys[] = {"DB_Path"};

static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);
static int nprocs;
static long int max_id = 0;

static char path_database[1024] = DEFAULT_DB_PATH;

static sqlite3 *db;

// checking if the file is not e.g. a symlink
static bool check_path_correct(const char *path) {

  struct stat sb;

  if (lstat(path, &sb) == -1) {
    WARNING("Failed stat check for file: %s", path);
    return false;
  }

  if (S_ISREG(sb.st_mode) == 0) {
    WARNING("Not a regular file: %s", path);
    return false;
  }

  return true;
}

// checking unsigned long long int overflow
static void safe_incremented_counter(unsigned long long *value) {
  if (*value == ULLONG_MAX) {
    WARNING("The counter can't be incremented");
    return;
  }
  *value += 1;
  return;
}

// checking and validating a string when trying to convert it to an long int
static bool convert_to_number(char *text, long int *number) {
  if (text == NULL) {
    WARNING("Error when trying to read a numeric value. NULL value");
    return false;
  }
  *number = strtol(text, NULL, 10);
  if (*number == STRTOL_ERROR_RET_VAL) {
    if (sizeof(text) == sizeof(char *) && text[0] == '0') {
      return true;
    } else {
      WARNING("Number is not an integer. Data read: %s", text);
      return false;
    }
  }

  if (*number < 0) {
    WARNING("Number can't be negative. Data read: %s", text);
    return false;
  }

  if (errno == ERANGE) {
    WARNING("Number can't be greater than LONG_MAX. Data read: %s", text);
    return false;
  }
  return true;
}

static int ras_config(const char *key, const char *value) {
  if (strcasecmp("DB_Path", key) == 0) {
    sstrncpy(path_database, value, sizeof(path_database));
  } else {
    DEBUG("DB_Path not provided in config. Using default: %s", DEFAULT_DB_PATH);
  }
  return 0;
} /* int ras_config */

static void ras_submit(const char *dev, const char *type, const char *type_inst,
                       unsigned long long value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.counter = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, RAS_PLUGIN, sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, dev, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));
  sstrncpy(vl.type_instance, type_inst, sizeof(vl.type_instance));
  plugin_dispatch_values(&vl);
}
/* void ras_submit */

// Assigning the error to the appropriate counter. e.g. error with error_msg
// contains "Microcode ROM parity error" and cpu 0, should increment counter
// ras_microcode_rom_parity_errors for the 0 cpu
static void classify_entries(int cpu, char *error_msg, char *mcistatus_msg) {

  if (strstr(error_msg, "Unclassified") ||
      strstr(error_msg, "Internal unclassified")) {
    safe_incremented_counter(
        &ras_metrics_server.per_CPU[cpu].ras_unclassified_mce_errors);
    safe_incremented_counter(
        &ras_metrics_server.per_CPU[cpu].ras_processor_base_errors);
  }
  if (strstr(error_msg, "Microcode ROM parity error")) {
    safe_incremented_counter(
        &ras_metrics_server.per_CPU[cpu].ras_microcode_rom_parity_errors);
    safe_incremented_counter(
        &ras_metrics_server.per_CPU[cpu].ras_processor_base_errors);
  }
  if (strstr(error_msg, "External error")) {
    safe_incremented_counter(
        &ras_metrics_server.per_CPU[cpu].ras_external_mce_errors);
    safe_incremented_counter(
        &ras_metrics_server.per_CPU[cpu].ras_processor_base_errors);
  }
  if (strstr(error_msg, "FRC error")) {
    safe_incremented_counter(&ras_metrics_server.per_CPU[cpu].ras_frc_errors);
    safe_incremented_counter(
        &ras_metrics_server.per_CPU[cpu].ras_processor_base_errors);
  }
  if (strstr(error_msg, "Internal parity error")) {
    safe_incremented_counter(
        &ras_metrics_server.per_CPU[cpu].ras_internal_parity_error);
    safe_incremented_counter(
        &ras_metrics_server.per_CPU[cpu].ras_processor_base_errors);
  }
  if (strstr(error_msg, "SMM Handler Code Access Violation")) {
    safe_incremented_counter(
        &ras_metrics_server.per_CPU[cpu]
             .ras_smm_handler_code_access_violation_errors);
    safe_incremented_counter(
        &ras_metrics_server.per_CPU[cpu].ras_processor_base_errors);
  }
  if (strstr(error_msg, "Internal Timer error")) {
    safe_incremented_counter(
        &ras_metrics_server.per_CPU[cpu].ras_internal_timer_errors);
    safe_incremented_counter(
        &ras_metrics_server.per_CPU[cpu].ras_processor_base_errors);
  }
  if (strstr(error_msg, "BUS") && strstr(error_msg, "Error")) {
    safe_incremented_counter(
        &ras_metrics_server.per_CPU[cpu].ras_processor_bus_errors);
  }
  if (strstr(error_msg, "Memory read error")) {
    if (strstr(mcistatus_msg, "Uncorrected_error")) {
      safe_incremented_counter(&ras_metrics_server.per_CPU[cpu]
                                    .ras_memory_read_uncorrectable_errors);
    } else {
      if (strstr(mcistatus_msg, "Corrected_error")) {
        safe_incremented_counter(
            &ras_metrics_server.per_CPU[cpu].ras_memory_read_corrected_errors);
      }
    }
  }
  if (strstr(error_msg, "Memory write error")) {
    if (strstr(mcistatus_msg, "Uncorrected_error")) {
      safe_incremented_counter(&ras_metrics_server.per_CPU[cpu]
                                    .ras_memory_write_uncorrectable_errors);
    } else {
      if (strstr(mcistatus_msg, "Corrected_error")) {
        safe_incremented_counter(
            &ras_metrics_server.per_CPU[cpu].ras_memory_write_corrected_errors);
      }
    }
  }

  if (((strstr(error_msg, "CACHE Level-0")) ||
       (strstr(error_msg, "CACHE Level-1"))) &&
      strstr(error_msg, "Error")) {
    safe_incremented_counter(
        &ras_metrics_server.per_CPU[cpu].ras_cache_l0_l1_errors);
  }
  if (strstr(error_msg, "Instruction TLB") && strstr(error_msg, "Error")) {
    safe_incremented_counter(
        &ras_metrics_server.per_CPU[cpu].ras_tlb_instruction_errors);
  }
  if (strstr(error_msg, "CACHE Level-2") && strstr(error_msg, "Error")) {
    safe_incremented_counter(&ras_metrics_server.ras_cache_l2_errors);
  }
  if (strstr(error_msg, "UPI:")) {
    safe_incremented_counter(&ras_metrics_server.ras_upi_errors);
  }
  return;
}

// function is invoked for each result row coming out of the evaluated SQL
// statements
static int callback(void *NotUsed, int argc, char **argv, char **azColName) {

  long int cpu;
  long int id;
  // argv[0] = id , argv[1] = cpu,  argv[2] = error_msg,
  // argv[3] = mcistatus_msg
  if (convert_to_number(argv[0], &id) && convert_to_number(argv[1], &cpu) &&
      argv[2] != NULL && argv[3] != NULL) {
    if (cpu <= nprocs) {
      classify_entries(cpu, argv[2], argv[3]);
    } else {
      WARNING("CPU number can't be greater than the total number of CPU. CPU: "
              "%ld",
              cpu);
      WARNING("Can't read data id %s, cpu %s, error_msg %s, mcistatus_msg %s",
              argv[0], argv[1], argv[2], argv[3]);
    }
  } else {
    WARNING("Can't read data id %s, cpu %s, error_msg %s, mcistatus_msg %s",
            argv[0], argv[1], argv[2], argv[3]);
  }

  if (max_id < id) {
    max_id = id;
  }

  return 0;
}

static void ras_submit_all_metrics() {
  ras_submit("", RAS_TYPE, "cache_l2", ras_metrics_server.ras_cache_l2_errors);
  ras_submit("", RAS_TYPE, "upi", ras_metrics_server.ras_upi_errors);

  char plugin_inst[PLUGIN_INST_SIZE];
  int cx;
  for (int i = 0; i < nprocs; i++) {
    cx = snprintf(plugin_inst, PLUGIN_INST_SIZE * sizeof(char), "CPU_%d", i);
    if (cx < 0 || cx >= PLUGIN_INST_SIZE * sizeof(char)) {
      ERROR("Error encountered during plugin's instance name creation");
      return;
    }

    ras_submit(plugin_inst, RAS_TYPE, "unclassified_mce",
               ras_metrics_server.per_CPU[i].ras_unclassified_mce_errors);
    ras_submit(plugin_inst, RAS_TYPE, "microcode_rom_parity",
               ras_metrics_server.per_CPU[i].ras_microcode_rom_parity_errors);

    ras_submit(plugin_inst, RAS_TYPE, "external_mce",
               ras_metrics_server.per_CPU[i].ras_external_mce_errors);

    ras_submit(plugin_inst, RAS_TYPE, "frc",
               ras_metrics_server.per_CPU[i].ras_frc_errors);

    ras_submit(plugin_inst, RAS_TYPE, "internal_parity",
               ras_metrics_server.per_CPU[i].ras_internal_parity_error);

    ras_submit(plugin_inst, RAS_TYPE, "smm_handler_code_access_violation",
               ras_metrics_server.per_CPU[i]
                   .ras_smm_handler_code_access_violation_errors);

    ras_submit(plugin_inst, RAS_TYPE, "internal_timer",
               ras_metrics_server.per_CPU[i].ras_internal_timer_errors);

    ras_submit(plugin_inst, RAS_TYPE, "processor_bus",
               ras_metrics_server.per_CPU[i].ras_processor_bus_errors);

    ras_submit(plugin_inst, RAS_TYPE, "processor_base",
               ras_metrics_server.per_CPU[i].ras_processor_base_errors);

    ras_submit(plugin_inst, RAS_TYPE, "memory_read_corrected",
               ras_metrics_server.per_CPU[i].ras_memory_read_corrected_errors);

    ras_submit(plugin_inst, RAS_TYPE, "memory_write_corrected",
               ras_metrics_server.per_CPU[i].ras_memory_write_corrected_errors);

    ras_submit(
        plugin_inst, RAS_TYPE, "memory_read_uncorrectable",
        ras_metrics_server.per_CPU[i].ras_memory_read_uncorrectable_errors);

    ras_submit(
        plugin_inst, RAS_TYPE, "memory_write_uncorrectable",
        ras_metrics_server.per_CPU[i].ras_memory_write_uncorrectable_errors);

    ras_submit(plugin_inst, RAS_TYPE, "cache_l0_l1",
               ras_metrics_server.per_CPU[i].ras_cache_l0_l1_errors);

    ras_submit(plugin_inst, RAS_TYPE, "tlb_instruction",
               ras_metrics_server.per_CPU[i].ras_tlb_instruction_errors);
  }
}

static int ras_read(void) {
  char *err_msg = 0;
  char sql_query[SQL_QUERY_BUFFER_SIZE];
  int rc;
  int cx;

  cx = snprintf(sql_query, SQL_QUERY_BUFFER_SIZE * sizeof(char),
                "select id, cpu, error_msg, mcistatus_msg from "
                "mce_record where id>%ld",
                max_id);

  if (cx < 0 || cx >= SQL_QUERY_BUFFER_SIZE * sizeof(char)) {
    ERROR("Error encountered during SQL query creation");
    return -1;
  }

  rc = sqlite3_exec(db, sql_query, callback, 0, &err_msg);
  if (rc != 0) {
    DEBUG("SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
    return -1;
  }
  ras_submit_all_metrics();

  return 0;
} /* int ras_read */

static int ras_init(void) {
  int rc;
  if (!check_path_correct(path_database)) {
    ERROR("Incorrect path to Database: %s", path_database);
    return -1;
  }
  rc = sqlite3_open_v2(path_database, &db, SQLITE_OPEN_READONLY, NULL);

  if (rc) {
    ERROR("Can't open database: %s", sqlite3_errmsg(db));
    return -1;
  } else {
    INFO("Database opened successfully");
  }

  long n = sysconf(_SC_NPROCESSORS_CONF);
  if (n == -1) {
    ERROR("ras plugin: sysconf(_SC_NPROCESSORS_CONF) failed: %s", STRERRNO);
    return errno;
  }
  if (n <= 0) {
    ERROR("ras plugin: sysconf(_SC_NPROCESSORS_CONF) returned %ld", n);
    return EINVAL;
  }

  nprocs = (int)n;
  ras_metrics_server.per_CPU = (struct ras_metrics_per_CPU *)calloc(
      nprocs, sizeof(struct ras_metrics_per_CPU));
  if (ras_metrics_server.per_CPU == NULL) {
    ERROR("Fail allocated memory");
    return ENOMEM;
  }
  return 0;
} /* int ras_init */

static int ras_shutdown(void) {
  sqlite3_close(db);
  free(ras_metrics_server.per_CPU);
  return 0;
}

void module_register(void) {
  plugin_register_config(RAS_PLUGIN, ras_config, config_keys, config_keys_num);
  plugin_register_init(RAS_PLUGIN, ras_init);
  plugin_register_read(RAS_PLUGIN, ras_read);
  plugin_register_shutdown(RAS_PLUGIN, ras_shutdown);
} /* void module_register */
