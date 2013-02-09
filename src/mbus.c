/**
 * collectd - src/mbus.c
 * Copyright (C) 2009  Tomas Menzl
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Tomas Menzl
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <string.h>
#include <pthread.h>

/* this is the standard lib, not the plugin!!! */
#include <mbus/mbus.h>
#include <mbus/mbus-protocol-aux.h>

/*
  Max number of records per slave?
  Minimal record is (empty one) 2B (VIF and VIB byte only).
  This means 234B (recommended max) / 2B = 117 records.
  Round up to 120b / 15B.
*/
#define MBUS_MAX_RECORDS      117
#define MBUS_MAX_RECORDS_SIZE ((MBUS_MAX_RECORDS+7)/8)

/**
 * Structure type representing configured MBus slave 
 * 
 */
typedef struct _mbus_slave {
    mbus_address        address; /**< Address of the slave */
    unsigned char       mask[MBUS_MAX_RECORDS_SIZE]; /**< Record mask - see e.g. #mbus_slave_record_add*/
    struct _mbus_slave *next_slave; /**< Next slave (also work as single linked list) */
} mbus_slave;

static pthread_mutex_t plugin_lock; /**< Global lock fof the MBus access */

/* Note that the MBus library is not thread safe (e.g. uses global data where */
/* the update may leave them in inconsistent state, etc.). On the other hand given the MBus */
/* nature - bus, synchronous communication (i.e. we can have only one operation in */
/* progress and have ot wait till it is finished) this is not a problem here for us. */
 
/* The only problem would be should we want to have multiple instances of MBus but */
/* this plugin represents only a single bus and collectd does not support multiple */
/* instances of a single plugin. */

/* Even though it is not necessary right now (the configure and init functions are */
/* called before worker threads are created, i.e. these callbacks are called */
/* sequentially) it is better to be prepared should the sequencing change (and */
/* also the sequnce is not obvious). */

/* The type handling is at the moment left for the standard mechanism of types.db */
/* (see the man pages). Adding another layer of type mapping just inside this module */
/* seems strange (at the moment) but perhaps will evolve over time as a generic facility */
/* for any module should there be a use case for it. */
/* Note that there are quite a number of record quantities (e.g. see the libmbus and the VIF */
/* decoding). */

static _Bool conf_is_serial;
static char *conf_device    = NULL;
static int   conf_baudrate  = 2400;
static char *conf_host      = NULL;
static int   conf_port      = 0;

static mbus_handle *handle = NULL; /**< MBus gateway */
static mbus_slave  *mbus_slaves;   /**< List of configured slaves */

/* =============================== MBus utils ================================= */

/** 
 * Allocates new structure repreesnting an MBus slave
 * 
 * @return created slave if successful, NULL otherwise
 */
static mbus_slave * mbus_slave_new(void)
{
    DEBUG("mbus: mbus_slave_new - creating new slave");
    mbus_slave * slave = (mbus_slave *) malloc(sizeof(*slave));
    if(slave == NULL)
        return NULL;
    
    memset(slave, 0, sizeof(*slave));
    slave->address.is_primary = 1;
    slave->address.secondary = NULL;
    slave->next_slave = NULL;
    return slave;
}

/** 
 * Initialize record filtering mask.
 *
 * Each slave supports record filtering - basically a bit array where
 * each bit represents whether given record (bit index/position) shall be
 * processed or not
 *
 * @param slave Existing MBus slave structure
 * @param clear If true the mask will be cleared (no records to processed). When
 *              false all bits are set (process all records)
 */
static void mbus_slave_init_mask(mbus_slave * slave, _Bool clear)
{
    if(clear)
    {
        DEBUG("mbus: mbus_slave_init_mask - clearing all");
        memset(slave->mask, 0x00, sizeof(slave->mask));
    }
    else
    {
        DEBUG("mbus: mbus_slave_init_mask - setting all");
        memset(slave->mask, 0xff, sizeof(slave->mask));
    }
}

/** 
 * Free previously created slave structure
 * 
 * @param slave Existing MBus slave structure
 */
static void mbus_slave_free(mbus_slave * slave)
{
    DEBUG("mbus: mbus_slave_free - deleting slave");
    if(slave == NULL)
        return;
    
    if(!(slave->address.is_primary))
        sfree(slave->address.secondary);

    free(slave);
}

/** 
 * Adds (enables for processing) given record
 * 
 * @param slave         Existing MBus slave structure
 * @param record_number Zero based record number/index to be added/enabled
 */
static void mbus_slave_record_add(mbus_slave * slave, int record_number)
{
    int byte=record_number/8;
    int bit =record_number%8;

    DEBUG("mbus: mbus_slave_record_add - adding record %d", record_number);
    slave->mask[byte] |= 1 << bit;
}

/** 
 * Removes (disables for processing) given record
 * 
 * @param slave         Existing MBus slave structure
 * @param record_number Zero based record number/index to be removed/disabled
 */
static void mbus_slave_record_remove(mbus_slave * slave, int record_number)
{
    int byte=record_number/8;
    int bit =record_number%8;

    DEBUG("mbus: mbus_slave_record_remove - removing record %d", record_number);
    slave->mask[byte] &= (0xff ^ (1<<bit));
}

/** 
 * Checks whether given record is enabled to be processed
 * 
 * @param slave         Existing MBus slave structure
 * @param record_number Zero based record number/index to be checked
 * 
 * @return One if enabled, zero when disabled
 */
static int mbus_slave_record_check(mbus_slave * slave, int record_number)
{
    int byte=record_number/8;
    int bit =record_number%8;

    int result = (slave->mask[byte] & (1<<bit)) ? 1 : 0;
    DEBUG("mbus: mbus_slave_record_check - checking record %d with result %d", record_number, result);
    return result;
}

/* =============================== CONFIGURATION ================================= */

/** 
 * Configure single MBus slave (i.e. process <Slave ..> element
 * 
 * @param ci Configuration to process
 * 
 * @return Zero when successful
 */
static int collectd_mbus_config_slave (oconfig_item_t *ci)
{
    mbus_slave     *slave           = NULL;
    mbus_address   *address         = NULL;
    int             i;
    oconfig_item_t *child           = NULL;
    _Bool           ignore_selected = 1;
    int             record_number;
    int             conf_res;
    size_t          str_len;

    slave = mbus_slave_new();
    if(slave == NULL)
    {
        ERROR("mbus: collectd_mbus_config_slave - cannot allocate new slave");
        return -2;
    }

    address = &(slave->address);

    if (ci->values_num != 1)
    {
        ERROR("mbus: collectd_mbus_config_slave - missing or wrong slave address");
        mbus_slave_free(slave);
        return -1;
    }

    if(ci->values[0].type == OCONFIG_TYPE_STRING)
    {
        address->is_primary = 0;
        address->secondary = NULL;
        conf_res = cf_util_get_string (ci, &(address->secondary));
        str_len = strlen(address->secondary);
        if(!conf_res && str_len==16)
        {
            DEBUG("mbus: collectd_mbus_config_slave - slave with primary address %s", address->secondary);
        }
        else
        {
            ERROR("mbus: collectd_mbus_config_slave - missing or wrong secondary slave address");
            mbus_slave_free(slave);
            return -1;
        }
    }
    else
    {
        if(ci->values[0].type == OCONFIG_TYPE_NUMBER)
        {
            address->is_primary = 1;
            conf_res = cf_util_get_int (ci, &(address->primary));
            if(!conf_res && address->primary<=250 && address->primary>=1)
            {
                DEBUG("mbus: collectd_mbus_config_slave - slave with primary address %d", address->primary);
            }
            else
            {
                ERROR("mbus: collectd_mbus_config_slave - wrong primary slave address (%d)", address->primary);
                mbus_slave_free(slave);
                return -1;
            }
        }
        else
        {
            ERROR("mbus: collectd_mbus_config_slave - missing or wrong slave address");
            mbus_slave_free(slave);
            return -1;
        }
    }
    
    /* first sort out the selection logic; last setting wins */
    for (i = 0; i < ci->children_num; i++)
    {
        child = ci->children + i;
        if (strcasecmp ("IgnoreSelected", child->key) == 0)
        {
            cf_util_get_boolean (child, &ignore_selected);
            DEBUG("mbus: collectd_mbus_config_slave - IgnoreSelected = %d", ignore_selected);
        }
    }

    /* initialize the record mask array */
    mbus_slave_init_mask(slave, !ignore_selected);

    /* now set/clear the configured records */
    for (i = 0; i < ci->children_num; i++)
    {
        child = ci->children + i;

        if (strcasecmp ("Record", child->key) == 0)
        {
            conf_res = cf_util_get_int (child, &record_number);
            if (!conf_res)
            {
                if(ignore_selected)
                    mbus_slave_record_remove(slave, record_number);
                else
                    mbus_slave_record_add(slave, record_number);
            }
        }
    }

    slave->next_slave = mbus_slaves;
    mbus_slaves = slave;
    return 0;
}


/** 
 * Main plugin configuration callback
 * 
 * @param ci Top level configuration
 * 
 * @return Zero when successful.
 */
static int collectd_mbus_config (oconfig_item_t *ci)
{
    int i;
    int result = 0;
    int conf_res = 0;   /**< signalling config parse outcome, zero is OK */
    
    DEBUG("==collectd_mbus_config==");

    pthread_mutex_lock (&plugin_lock);
    
    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;
        
        if (strcasecmp ("IsSerial", child->key) == 0)
        {
            conf_res = cf_util_get_boolean (child, &conf_is_serial);
        }
        else 
        {
            if (strcasecmp ("SerialDevice", child->key) == 0)
            {
                conf_res = cf_util_get_string (child, &conf_device);
            }
            else 
            {
                if (strcasecmp ("BaudRate", child->key) == 0)
                {
                    conf_res = cf_util_get_int (child, &conf_baudrate);
                }
                else
                {
                    if (strcasecmp ("Host", child->key) == 0)
                    {
                        conf_res = cf_util_get_string (child, &conf_host);
                    }
                    else
                    {
                        if (strcasecmp ("Port", child->key) == 0)
                        {
                            conf_res = cf_util_get_port_number (child);
                            if (conf_res > 0)
                            {
                                conf_port = conf_res;
                                conf_res = 0;
                            }
                        }
                        else
                        { 
                            if (strcasecmp ("Slave", child->key) == 0)
                            {
                                conf_res = collectd_mbus_config_slave (child);
                            }
                            else
                                WARNING ("mbus: collectd_mbus_config - unknown config option or unsupported config value: %s", 
                                         child->key);
                        }
                    }
                }
            }
        }
        if (conf_res)
            break;
    }
    
    pthread_mutex_unlock(&plugin_lock);

    DEBUG("mbus: collectd_mbus_config - IsSerial = %d", conf_is_serial);

    if(conf_res)
    {
        ERROR("mbus: collectd_mbus_config - configuration failed");
        result = -1;
    }
    else if(conf_is_serial)
    {
        if(conf_device)
            DEBUG("mbus: collectd_mbus_config - Device = %s", conf_device);
        else
        { 
            ERROR("mbus: collectd_mbus_config - Serial device not configured");
            result = -1;
        }
    }
    else
    {
        if(conf_host)
            DEBUG("mbus: collectd_mbus_config - Host = %s", conf_host);
        else
        {
            ERROR("mbus: collectd_mbus_config - Host not configured");
            result = -1;
        }

        if(conf_port > 0)
            DEBUG("mbus: collectd_mbus_config - Port = %d", conf_port);
        else
        {
            ERROR("mbus: collectd_mbus_config - Port not configured");
            result = -1;
        }
    }
    
    return (result);
}




/* =============================== INIT ================================= */

/** 
 * Initialization callback
 * 
 * Only connects to the MBus gateway.
 * 
 * @return Zero when successful.
 */
static int collectd_mbus_init (void)
{
    DEBUG("mbus: collectd_mbus_init");

    pthread_mutex_lock (&plugin_lock);
    if(conf_is_serial > 0)
    {
        handle = mbus_context_serial(conf_device);
        if (handle == NULL)
        {
            ERROR("mbus: mbus_context_serial - Failed to setup serial context");
            pthread_mutex_unlock (&plugin_lock);
            return -1;
        }
    }
    else
    {
        handle = mbus_context_tcp(conf_host, conf_port);
        if (handle == NULL)
        {
            ERROR("mbus: mbus_context_serial - Failed to setup TCP context");
            pthread_mutex_unlock (&plugin_lock);
            return -1;
        }
    }

    if (mbus_connect(handle) != 0)
    {
        ERROR("mbus: mbus_connect - Failed to connect to the M-bus gateway");
        pthread_mutex_unlock (&plugin_lock);
        return -1;
    }

    if(conf_is_serial > 0)
    {
        if (mbus_serial_set_baudrate(handle, conf_baudrate) == -1)
        {
            ERROR("mbus: collectd_mbus_init - Failed to setup serial connection baudrate to %d",
                  conf_baudrate);
            pthread_mutex_unlock (&plugin_lock);
            return -1;
        }
        else
        {
            DEBUG("mbus: collectd_mbus_init - set serial connection baudrate to %d",
                  conf_baudrate);
        }
    }

    pthread_mutex_unlock (&plugin_lock);
    return (0);
}

/* =============================== SHUTDOWN ================================= */

/** 
 * Shutdown callback.
 * 
 * Disconnect the MBus gateway, delete it and delete all slaves.
 * 
 * @return Zero when successful (at the moment the only possible outcome)
 */
static int collectd_mbus_shutdown (void)
{
    mbus_slave * current_slave = mbus_slaves;
    mbus_slave * next_slave = NULL;

    DEBUG("mbus: collectd_mbus_shutdown");

    pthread_mutex_lock (&plugin_lock);
    if(handle != NULL)
    {
        if( mbus_disconnect(handle) != 0)
        {
            ERROR("mbus: collectd_mbus_shutdown - Failed to disconnect from the M-bus gateway");
        }
        else
            mbus_context_free(handle);
    }
        
    while(current_slave != NULL)
    {
        next_slave = current_slave->next_slave;
        mbus_slave_free(current_slave);
        current_slave = next_slave;
    }

    pthread_mutex_unlock (&plugin_lock);

    return (0);
}

/* =============================== READ ================================= */

/** 
 * Replace character in a null terminated string (in place).
 * 
 * Replaces all occurances of #search with #replace.
 * Helper function used to replace ' ' in value.type as types.db apparently
 * does not support it.
 *
 * @param string  Null terminated string in which we want to do the replacement.
 * @param search  Character we wnat to replace.
 * @param replace Character to be used as a replacement of #search
 */
static void str_char_replace(char * string, char search, char replace)
{
    while(*string != '\0')
    {
        if(*string == search)
            *string = replace;

        ++string;
    }
}


/** 
 * Parses MBus frame (fixed format) with  of a single slave and submits its
 * data to the collectd framework.
 *
 * Only to be called from #parse_and_submit
 * 
 * @param slave      MBus slave which frame we are processing.
 * @param vl         Collectd value list to be cmopleted and submitted.
 * @param frame_data MBus frame data to be parsed
 */
static void parse_and_submit_fixed (mbus_slave *    slave,
                                    value_list_t    vl,
                                    mbus_frame_data frame_data)
{
    char            *tmp_string;
    int              tmp_int;
    value_t          values[1];
    mbus_data_fixed *data;
    mbus_record     *record;
    int              result;

    DEBUG("mbus: parse_and_submit_fixed");

    data = &(frame_data.data_fix);
    
    /* id is 32bit / 4bytes / 8 digit bcd */
    tmp_int = mbus_data_bcd_decode(data->id_bcd, 4);
    DEBUG("mbus: parse_and_submit_fixed -     Id            = %d", tmp_int);
    ssnprintf (vl.type, sizeof (vl.type), "%d", tmp_int);
    
    tmp_string = (char*) mbus_data_fixed_medium(data);
    DEBUG("mbus: parse_and_submit_fixed -     Medium        = %s", tmp_string);
    sstrncpy (vl.type_instance, tmp_string, sizeof (vl.type_instance));
    
    result = mbus_slave_record_check(slave,0);
    if(!result)
    {
        DEBUG("mbus: parse_and_submit_fixed -   Record #0 disabled by mask");
    }
    else
    {
        DEBUG("mbus: parse_and_submit_fixed -   Record #0 enabled by mask");
        record = mbus_parse_fixed_record(data->status, data->cnt1_type, data->cnt1_val);
        if(NULL != record)
        {
            DEBUG("mbus: parse_and_submit_fixed -   Record #0");
            
            str_char_replace(record->quantity, ' ', '_');
            DEBUG("mbus: parse_and_submit_fixed -     Type            = %s", record->quantity);
            sstrncpy (vl.type, record->quantity, sizeof (vl.type));
            
            DEBUG("mbus: parse_and_submit_fixed -     Type instance   = 0");
            sstrncpy (vl.type_instance, "0", sizeof (vl.type_instance));
            
            if(record->quantity == NULL)
            {
                WARNING("mbus: parse_and_submit_fixed - missing quantity for record #0");
            }
            else
            {
                if(record->is_numeric)
                    values[0].gauge = record->value.real_val;
                else
                    values[0].gauge = NAN;
                
                DEBUG("mbus: parse_and_submit_fixed -     Value           = %lf", values[0].gauge);
                
                mbus_record_free(record);
                record = NULL;
                
                vl.values_len = 1;
                vl.values = values;
                plugin_dispatch_values (&vl);
            }
        }
        else
        {
            ERROR("mbus: parse_and_submit_fixed - failed parsing fixed record");
            values[0].gauge = NAN;
        }
    }
    
    result = mbus_slave_record_check(slave, 1);
    if(!result)
    {
        DEBUG("mbus: parse_and_submit_fixed -   Record #1 disabled by mask");
    } 
    else
    {
        DEBUG("mbus: parse_and_submit_fixed -   Record #1 enabled by mask");
        record = mbus_parse_fixed_record(data->status, data->cnt2_type, data->cnt2_val);
        if(NULL != record)
        {
            DEBUG("mbus: parse_and_submit_fixed -   Record #1");
                
            str_char_replace(record->quantity, ' ', '_');
            DEBUG("mbus: parse_and_submit_fixed -     Type            = %s", record->quantity);
            sstrncpy (vl.type, record->quantity, sizeof (vl.type));
            
            DEBUG("mbus: parse_and_submit_fixed -     Type instance   = 1");
            sstrncpy (vl.type_instance, "1", sizeof (vl.type_instance));
            
            if(record->quantity == NULL)
            {
                WARNING("mbus: parse_and_submit_fixed - missing quantity for record #0");
            }
            else
            {
                if(record->is_numeric)
                    values[0].gauge = record->value.real_val;
                else
                    values[0].gauge = NAN;
                
                DEBUG("mbus: parse_and_submit_fixed -     Value           = %lf", values[0].gauge);
                
                mbus_record_free(record);
                record = NULL;
                
                vl.values_len = 1;
                vl.values = values;
                plugin_dispatch_values (&vl);
            }
        }
        else
        {
            ERROR("mbus: parse_and_submit_fixed - failed parsing fixed record");
        }
    }
}


/** 
 * Parses MBus frame (variable format) with  of a single slave and submits its
 * data to the collectd framework.
 *
 * Only to be called from #parse_and_submit
 * 
 * @param slave MBus slave which frame we are processing.
 * @param vl    Collectd value list to be cmopleted and submitted.
 * @param frame_data MBus frame data to be parsed
 */
static void parse_and_submit_variable (mbus_slave *    slave,
                                       value_list_t    vl,
                                       mbus_frame_data frame_data)
{
    mbus_data_variable *data;
    mbus_data_record   *data_record;
    size_t              i;
    mbus_record        *record;
    value_t             values[1];
    int                 result;
    
    DEBUG("mbus: parse_and_submit_variable -   Variable record");
    data = &(frame_data.data_var);
    
    for (data_record = data->record, i = 0; 
         data_record != NULL; 
         data_record = (mbus_data_record *) (data_record->next), i++)
    {
        result = mbus_slave_record_check(slave, i);
        if(!result)
        {
            DEBUG("mbus: parse_and_submit_variable -   Record #%d disabled by mask", (int) i);
        }
        else
        {
            DEBUG("mbus: parse_and_submit_variable -   Record #%d enabled by mask", (int) i);
            
            record = mbus_parse_variable_record(data_record);
            if(NULL != record)
            {
                if(record->quantity == NULL)
                {
                    WARNING("mbus: parse_and_submit_variable - missing quantity for record #%d", (int) i);
                    mbus_record_free(record);
                    record = NULL;
                    continue;
                }
                
                DEBUG("mbus: parse_and_submit_variable -   Record %d", (int) i);
                
                str_char_replace(record->quantity, ' ', '_');
                DEBUG("mbus: parse_and_submit_variable -     Type            = %s", record->quantity);
                sstrncpy (vl.type, record->quantity, sizeof (vl.type));
                
                DEBUG("mbus: parse_and_submit_variable -     Type instance   = %d", (int) i);
                ssnprintf (vl.type_instance, sizeof (vl.type_instance), "%d", i);
                
                if(record->is_numeric)
                    values[0].gauge = record->value.real_val;
                else
                    values[0].gauge = NAN;
                
                DEBUG("mbus: parse_and_submit_variable -     Value           = %lf", values[0].gauge);
                
                mbus_record_free(record);
                record = NULL;
                
                vl.values_len = 1;
                vl.values = values;
                plugin_dispatch_values (&vl);
            }
            else
            {
                ERROR("mbus: parse_and_submit_variable - failed parsing variable record");
            }
        }
    }
}


/** 
 * Parses MBus frame of a single slave and submits its data to the collectd framework.
 * 
 * @param slave MBus slave which frame we are processing.
 * @param frame Frame to process.
 * 
 * @return Zero when successful.
 */
static int parse_and_submit (mbus_slave * slave, mbus_frame * frame)
{
    value_list_t    vl = VALUE_LIST_INIT;
    mbus_frame_data frame_data;
    mbus_address   *address;
    int             result;

    DEBUG("mbus: parse_and_submit");
    
    if(slave == NULL)
    {
        ERROR("mbus: parse_and_submit - NULL slave");
        return -2;
    }
    address = &(slave->address);
    sstrncpy (vl.host, hostname_g, sizeof (vl.host));
    sstrncpy (vl.plugin, "mbus", sizeof (vl.plugin));
    
    if(address->is_primary)
    {
        ssnprintf (vl.plugin_instance, sizeof (vl.plugin_instance), "%d", address->primary);
    }
    else
    {
        sstrncpy (vl.plugin_instance, 
                  address->secondary,
                  sizeof (vl.plugin_instance));
    }
    
    result = mbus_frame_data_parse(frame, &frame_data);
    if(result == -1)
    {
        ERROR("mbus: parse_and_submit - failed mbus_frame_data_parse");
        return -1;
    }
    
    /*  ----------------------------- fixed structure ----------------------- */
    if (frame_data.type == MBUS_DATA_TYPE_FIXED)
    {
        parse_and_submit_fixed (slave, vl, frame_data);
    }
    else
    {
        /* -------------------------- variable structure ---------------------- */
        if (frame_data.type == MBUS_DATA_TYPE_VARIABLE)
        {
            parse_and_submit_variable (slave, vl, frame_data);
        }
    }
    
    if(frame_data.data_var.record)
    {
        mbus_data_record_free(frame_data.data_var.record);
    }
    
    return 0;
}


/** 
 * Plugin read callback.
 * 
 * @return Zero when successful.
 */
static int collectd_mbus_read (void)
{
    mbus_frame reply;
    mbus_slave * current_slave;
    int        result;

    DEBUG("mbus: collectd_mbus_read");
    pthread_mutex_lock (&plugin_lock);

    for(current_slave = mbus_slaves; current_slave != NULL; current_slave = current_slave->next_slave)
    {
        result = mbus_read_slave(handle, &(current_slave->address), &reply);
        if(result)
        {
            if(current_slave->address.is_primary)
            {
                ERROR("mbus: collectd_mbus_read - problem reading slave at primary address %d",
                      current_slave->address.primary);
            }
            else
            {
                ERROR("mbus: collectd_mbus_read - problem reading slave at secondary address %s",
                      current_slave->address.secondary);
            }
            continue;
        }

        parse_and_submit(current_slave, &reply);
    }

    pthread_mutex_unlock (&plugin_lock);
    return (0);
}


/* =============================== REGISTER ================================= */

/** 
 * PLugin "entry" - register all callback.
 * 
 */
void module_register (void)
{
    plugin_register_complex_config ("mbus", collectd_mbus_config);
    plugin_register_init ("mbus", collectd_mbus_init);
    plugin_register_shutdown ("mbus", collectd_mbus_shutdown);
    plugin_register_read ("mbus", collectd_mbus_read);
}
