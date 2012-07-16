/**
 * collectd - src/barometer.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; only version 2.1 of the License is
 * applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *   Tomas Menzl
 **/

#include "collectd.h"
#include "common.h"
#include "utils_cache.h"
#include "plugin.h"

#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <math.h>

/* ------------ MPL115 defines ------------ */
/* I2C address of the MPL115 sensor */
#define MPL115_I2C_ADDRESS          0x60
                                    
/* register addresses */            
#define MPL115_ADDR_CONV            0x00
#define MPL115_ADDR_COEFFS          0x04
                                    
/* register sizes */                
#define MPL115_NUM_CONV             4
#define MPL115_NUM_COEFFS           12
                                    
/* commands / addresses */          
#define MPL115_CMD_CONVERT_PRESS    0x10
#define MPL115_CMD_CONVERT_TEMP     0x11
#define MPL115_CMD_CONVERT_BOTH     0x12
                                    
#define MPL115_CONVERSION_RETRIES   5


/* ------------ MPL3115 defines ------------ */
/* MPL3115 I2C address */
#define MPL3115_I2C_ADDRESS         0x60

/* register addresses (only the interesting ones) */
#define MPL3115_REG_STATUS          0x00
#define MPL3115_REG_OUT_P_MSB       0x01
#define MPL3115_REG_OUT_P_CSB       0x02
#define MPL3115_REG_OUT_P_LSB       0x03
#define MPL3115_REG_OUT_T_MSB       0x04
#define MPL3115_REG_OUT_T_LSB       0x05
#define MPL3115_REG_DR_STATUS       0x06
#define MPL3115_REG_WHO_AM_I        0x0C
#define MPL3115_REG_SYSMOD          0x11
#define MPL3115_REG_PT_DATA_CFG     0x13
#define MPL3115_REG_BAR_IN_MSB      0x14
#define MPL3115_REG_BAR_IN_LSB      0x15
#define MPL3115_REG_CTRL_REG1       0x26
#define MPL3115_REG_CTRL_REG2       0x27
#define MPL3115_REG_CTRL_REG3       0x28
#define MPL3115_REG_CTRL_REG4       0x29
#define MPL3115_REG_CTRL_REG5       0x2A
#define MPL3115_REG_OFF_P           0x2B
#define MPL3115_REG_OFF_T           0x2C
#define MPL3115_REG_OFF_H           0x2D

/* Register values, masks */
#define MPL3115_WHO_AM_I_RESP       0xC4

#define MPL3115_PT_DATA_DREM        0x04
#define MPL3115_PT_DATA_PDEF        0x02
#define MPL3115_PT_DATA_TDEF        0x01
                                    
#define MPL3115_DR_STATUS_TDR       0x02
#define MPL3115_DR_STATUS_PDR       0x04
#define MPL3115_DR_STATUS_PTDR      0x08
#define MPL3115_DR_STATUS_DR        (MPL3115_DR_STATUS_TDR | MPL3115_DR_STATUS_PDR | MPL3115_DR_STATUS_PTDR)
                                    
#define MPL3115_DR_STATUS_TOW       0x20
#define MPL3115_DR_STATUS_POW       0x40
#define MPL3115_DR_STATUS_PTOW      0x80

#define MPL3115_CTRL_REG1_ALT       0x80
#define MPL3115_CTRL_REG1_RAW       0x40
#define MPL3115_CTRL_REG1_OST_MASK  0x38
#define MPL3115_CTRL_REG1_OST_1     0x00
#define MPL3115_CTRL_REG1_OST_2     0x08
#define MPL3115_CTRL_REG1_OST_4     0x10
#define MPL3115_CTRL_REG1_OST_8     0x18
#define MPL3115_CTRL_REG1_OST_16    0x20
#define MPL3115_CTRL_REG1_OST_32    0x28
#define MPL3115_CTRL_REG1_OST_64    0x30
#define MPL3115_CTRL_REG1_OST_128   0x38
#define MPL3115_CTRL_REG1_RST       0x04
#define MPL3115_CTRL_REG1_OST       0x02
#define MPL3115_CTRL_REG1_SBYB      0x01
#define MPL3115_CTRL_REG1_SBYB_MASK 0xFE

#define MPL3115_NUM_CONV_VALS       5


/* ------------ Normalization ------------ */
/* Mean sea level pressure normalization methods */
#define MSLP_NONE          0
#define MSLP_INTERNATIONAL 1
#define MSLP_DEU_WETT      2

/** Temperature reference history depth for averaging. See #get_reference_temperature */
#define REF_TEMP_AVG_NUM   5

/* ------------------------------------------ */
static const char *config_keys[] =
{
    "Device",
    "Oversampling",
    "PressureOffset",    /**< only for MPL3115 */
    "TemperatureOffset", /**< only for MPL3115 */
    "Altitude",
    "Normalization",
    "TemperatureSensor"
};

static int    config_keys_num     = STATIC_ARRAY_SIZE(config_keys);
                                  
static char * config_device       = NULL;  /**< I2C bus device */
static int    config_oversample   = 1;     /**< averaging window */

static double config_press_offset = 0.0;   /**< pressure offset */
static double config_temp_offset  = 0.0;   /**< temperature offset */

static double config_altitude     = NAN;   /**< altitude */
static int    config_normalize    = 0;     /**< normalization method */
                                  
static _Bool  configured          = 0;     /**< the whole plugin config status */
                                  
static int    i2c_bus_fd          = -1;    /**< I2C bus device FD */
                                  
static _Bool  is_MPL3115          = 0;    /**< is this MPL3115? */
static __s32  oversample_MPL3115  = 0;    /**< MPL3115 CTRL1 oversample setting */


/* MPL115 conversion coefficients */
static double mpl115_coeffA0;
static double mpl115_coeffB1;
static double mpl115_coeffB2;
static double mpl115_coeffC12;
static double mpl115_coeffC11;
static double mpl115_coeffC22;

/* ------------------------ averaging ring buffer ------------------------ */
/*  Used only for MPL115. MPL3115 supports real oversampling in the device so */
/*  no need for any postprocessing. */

static _Bool avg_initialized = 0;    /**< already initialized by real values */

typedef struct averaging_s {
    long int * ring_buffer;
    int        ring_buffer_size;
    long int   ring_buffer_sum;
    int        ring_buffer_head;
} averaging_t;


static averaging_t pressure_averaging    = { NULL, 0, 0L, 0 };
static averaging_t temperature_averaging = { NULL, 0, 0L, 0 };


/** 
 * Create / allocate averaging buffer
 *
 * The buffer is initialized with zeros.
 *
 * @param avg  pointer to ring buffer to be allocated
 * @param size requested buffer size
 *
 * @return Zero when successful
 */
static int averaging_create(averaging_t * avg, int size)
{
    int a;

    avg->ring_buffer = (long int *) malloc(size * sizeof(*avg));
    if (avg->ring_buffer == NULL)
    {
        ERROR ("barometer: averaging_create - ring buffer allocation of size %d failed",
               size);
        return -1;
    }

    for (a=0; a<size; ++a)
    {
      avg->ring_buffer[a] = 0L;
    }

    avg->ring_buffer_size = size;
    avg->ring_buffer_sum  = 0L;
    avg->ring_buffer_head = 0;

    return 0;
}


/**
 * Delete / free existing averaging buffer
 *
 * @param avg  pointer to the ring buffer to be deleted
 */
static void averaging_delete(averaging_t * avg)
{
    if (avg->ring_buffer != NULL)
    {
        free(avg->ring_buffer);
        avg->ring_buffer = NULL;
    }
    avg->ring_buffer_size = 0;
    avg->ring_buffer_sum  = 0L;
    avg->ring_buffer_head = 0;
}


/*
 * Add new sample to the averaging buffer
 *
 * A new averaged value is returned. Note that till the buffer is full
 * returned value is inaccurate as it is an average of real values and initial
 * zeros.
 *
 * @param avg    pointer to the ring buffer
 * @param sample new sample value
 *
 * @return Averaged sample value
 */
static double averaging_add_sample(averaging_t * avg, long int sample)
{
    double result;

    avg->ring_buffer_sum += sample - avg->ring_buffer[avg->ring_buffer_head];
    avg->ring_buffer[avg->ring_buffer_head] = sample;
    avg->ring_buffer_head = (avg->ring_buffer_head+1) % avg->ring_buffer_size;
    result = (double)(avg->ring_buffer_sum) / (double)(avg->ring_buffer_size);
    
    DEBUG ("barometer: averaging_add_sample - added %ld, result = %lf", 
           sample, 
           result);

    return result;
}


/* ------------------------ temperature refference ------------------------ */

/**
 * Linked list type of temperature sensor references
 */
typedef struct temperature_list_s {
    char                      * sensor_name; /**< sensor name/reference */
    size_t                      num_values;  /**< number of values (usually one) */
    _Bool                       initialized; /**< sensor already provides data */
    struct temperature_list_s * next;        /**< next in the list */
} temperature_list_t;

static temperature_list_t * temp_list = NULL;


/*
 * Add new sensor to the temperature reference list
 *
 * @param list   the list
 * @param sensor reference name (as provided by the config file)
 *
 * @return Zero when successful
 */
static int temp_list_add(temperature_list_t * list, const char * sensor)
{
    temperature_list_t * new_temp;

    new_temp = (temperature_list_t *) malloc(sizeof(*new_temp));
    if(new_temp == NULL)
        return -1;

    new_temp->sensor_name = strdup(sensor);
    new_temp->initialized = 0;
    new_temp->num_values = 0;
    if(new_temp->sensor_name == NULL)
    {
        free(new_temp);
        return -1;
    }

    new_temp->next = temp_list;
    temp_list = new_temp;
    return 0;
}


/*
 * Delete the whole temperature reference list
 *
 * @param list the list to be deleted
 */
static void temp_list_delete(temperature_list_t ** list)
{
    temperature_list_t * tmp;

    while (*list != NULL)
    {
        tmp = (*list);
        (*list) = (*list)->next;
        free(tmp->sensor_name);
        free(tmp);
        tmp = NULL;
    }
}


/*
 * Get reference temperature value
 *
 * First initially uc_get_rate_by_name is tried. At the startup due to nondeterministic
 * order the temperature may not be read yet (then it fails and first measurment gives
 * only absolute air pressure reading which is acceptable). Once it succedes (should be
 * second measurement at the latest) we use average of few last readings from
 * uc_get_history_by_name. It may take few readings to start filling so again we use
 * uc_get_rate_by_name as a fallback.
 * The idea is to use basic "noise" filtering (history averaging) across all the values
 * which given sensor provides (up to given depth). Then we get minimum among
 * the sensors.
 *
 * @param result where the result is stored. When not available NAN is stored.
 *
 * @return Zero when successful
 */
static int get_reference_temperature(double * result)
{
    temperature_list_t * list = temp_list;

    gauge_t * values = NULL;   /**< rate values */
    size_t    values_num = 0;  /**< number of rate values */
    int i;

    gauge_t values_history[REF_TEMP_AVG_NUM];

    double avg_sum;  /**< Value sum for computing average */
    int    avg_num;  /**< Number of values for computing average */
    double average;  /**< Resulting value average */

    *result = NAN;

    while(list != NULL)
    {
        avg_sum = 0.0;
        avg_num = 0;

        /* First time need to read current rate to learn how many values are
           there (typically for temperature it would be just one).
           We do not expect dynamic changing of number of temperarure values
           in runtime yet (are there any such cases?). */
        if(!list->initialized)
        {
            if(uc_get_rate_by_name(list->sensor_name,
                                   &values,
                                   &values_num))
            {
                DEBUG ("barometer: get_reference_temperature - rate \"%s\" not found yet",
                       list->sensor_name);
                list = list->next;
                continue;
            }

            DEBUG ("barometer: get_reference_temperature - initialize \"%s\", %zu vals",
                   list->sensor_name,
                   values_num);

            list->initialized = 1;
            list->num_values = values_num;

            for(i=0; i<values_num; ++i)
            {
                DEBUG ("barometer: get_reference_temperature - rate %d: %lf **",
                       i,
                       values[i]);
                if(!isnan(values[i]))
                {
                    avg_sum += values[i];
                    ++avg_num;
                }
            }
            free(values);
            values = NULL;
        }

        /* It is OK to get here the first time as well, in the worst case
           the history will full of NANs. */
        if(uc_get_history_by_name(list->sensor_name,
                                  values_history,
                                  REF_TEMP_AVG_NUM,
                                  list->num_values))
        {
            ERROR ("barometer: get_reference_temperature - history \"%s\" lost",
                   list->sensor_name);
            list->initialized = 0;
            list->num_values = 0;
            list = list->next;
            continue;
        }
            
        for(i=0; i<REF_TEMP_AVG_NUM*list->num_values; ++i)
        {
            DEBUG ("barometer: get_reference_temperature - history %d: %lf",
                   i,
                   values_history[i]);
            if(!isnan(values_history[i]))
            {
                avg_sum += values_history[i];
                ++avg_num;
            }
        }

        if(avg_num == 0)   /* still no history? fallback to current */
        {
            if(uc_get_rate_by_name(list->sensor_name,
                                   &values,
                                   &values_num))
            {
                ERROR ("barometer: get_reference_temperature - rate \"%s\" lost",
                       list->sensor_name);
                list->initialized = 0;
                list->num_values = 0;
                list = list->next;
                continue;
            }

            for(i=0; i<values_num; ++i)
            {
                DEBUG ("barometer: get_reference_temperature - rate last %d: %lf **",
                       i,
                       values[i]);
                if(!isnan(values[i]))
                {
                    avg_sum += values[i];
                    ++avg_num;
                }
            }
            free(values);
            values = NULL;
        }

        if(avg_num == 0)
        {
            ERROR ("barometer: get_reference_temperature - could not read \"%s\"",
                   list->sensor_name);
            list->initialized = 0;
            list->num_values = 0;
        }
        else
        {
            average = avg_sum / (double) avg_num;
            if(isnan(*result))
                *result=average;
            else if(*result>average)
                *result=average;
        }
        list = list->next;
    }  /* while sensor list */
    
    if(*result == NAN)
    {
        ERROR("barometer: get_reference_temperature - no sensor available (yet?)");
        return -1;
    }
    DEBUG ("barometer: get_reference_temperature - temp is %lf", *result);
    return 0;
}

/* ------------------------ MPL115 access ------------------------ */

/** 
 * Read the MPL115 sensor conversion coefficients.
 *
 * These are (device specific) constants so we can read them just once.
 *
 * @return Zero when successful
 */
static int MPL115_read_coeffs(void)
{
    uint8_t mpl115_coeffs[MPL115_NUM_COEFFS]; 
    int32_t res;

    int8_t  sia0MSB, sia0LSB, sib1MSB, sib1LSB, sib2MSB, sib2LSB;
    int8_t  sic12MSB, sic12LSB, sic11MSB, sic11LSB, sic22MSB, sic22LSB;
    int16_t sia0, sib1, sib2, sic12, sic11, sic22;
      
    char errbuf[1024];

    res = i2c_smbus_read_i2c_block_data(i2c_bus_fd, 
                                        MPL115_ADDR_COEFFS, 
                                        MPL115_NUM_COEFFS, 
                                        mpl115_coeffs);
    if (res < 0)
    {
        ERROR ("barometer: read_mpl115_coeffs - problem reading data: %s",
               sstrerror (errno, errbuf, sizeof (errbuf)));
        return -1;
    }
   
    /* Using perhaps less elegant/efficient code, but more readable. */
    /* a0: 16total 1sign 12int 4fract 0pad */
    sia0MSB = mpl115_coeffs[0];
    sia0LSB = mpl115_coeffs[1];
    sia0 = (int16_t) sia0MSB <<8;          /* s16 type, Shift to MSB */
    sia0 += (int16_t) sia0LSB & 0x00FF;    /* Add LSB to 16bit number */
    mpl115_coeffA0 = (double) (sia0);
    mpl115_coeffA0 /= 8.0;                 /* 3 fract bits */
    
    /* b1: 16total 1sign 2int 13fract 0pad */
    sib1MSB= mpl115_coeffs[2];
    sib1LSB= mpl115_coeffs[3];
    sib1 = sib1MSB <<8;                    /* Shift to MSB */
    sib1 += sib1LSB & 0x00FF;              /* Add LSB to 16bit number */
    mpl115_coeffB1 = (double) (sib1);
    mpl115_coeffB1 /= 8192.0;              /* 13 fract */
    
    /* b2: 16total 1sign 1int 14fract 0pad */
    sib2MSB= mpl115_coeffs[4];
    sib2LSB= mpl115_coeffs[5];
    sib2 = sib2MSB <<8;                     /* Shift to MSB */
    sib2 += sib2LSB & 0x00FF;               /* Add LSB to 16bit number */
    mpl115_coeffB2 = (double) (sib2);
    mpl115_coeffB2 /= 16384.0;              /* 14 fract */

    /* c12: 14total 1sign 0int 13fract 9pad */
    sic12MSB= mpl115_coeffs[6];
    sic12LSB= mpl115_coeffs[7];
    sic12 = sic12MSB <<8;                   /* Shift to MSB only by 8 for MSB */
    sic12 += sic12LSB & 0x00FF;
    mpl115_coeffC12 = (double) (sic12);
    mpl115_coeffC12 /= 4.0;                 /* 16-14=2 */
    mpl115_coeffC12 /= 4194304.0;           /* 13+9=22 fract */

    /* c11: 11total 1sign 0int 11fract 11pad */
    sic11MSB= mpl115_coeffs[8];
    sic11LSB= mpl115_coeffs[9];
    sic11 = sic11MSB <<8;                   /* Shift to MSB only by 8 for MSB */
    sic11 += sic11LSB & 0x00FF;
    mpl115_coeffC11 = (double) (sic11);
    mpl115_coeffC11 /= 32.0;               /* 16-11=5 */
    mpl115_coeffC11 /= 4194304.0;          /* 11+11=22 fract */

    /* c12: 11total 1sign 0int 10fract 15pad */
    sic22MSB= mpl115_coeffs[10];
    sic22LSB= mpl115_coeffs[11];
    sic22 = sic22MSB <<8;                   /* Shift to MSB only by 8 for MSB */
    sic22 += sic22LSB & 0x00FF;
    mpl115_coeffC22 = (double) (sic22);
    mpl115_coeffC22 /= 32.0; //16-11=5
    mpl115_coeffC22 /= 33554432.0;          /* 10+15=25 fract */

    DEBUG("barometer: read_mpl115_coeffs: a0=%lf, b1=%lf, b2=%lf, c12=%lf, c11=%lf, c22=%lf",
          mpl115_coeffA0, 
          mpl115_coeffB1, 
          mpl115_coeffB2, 
          mpl115_coeffC12, 
          mpl115_coeffC11, 
          mpl115_coeffC22);
    return 0;
}


/*
 * Convert raw adc values to real data using the sensor coefficients.
 *
 * @param adc_pressure adc pressure value to be converted
 * @param adc_temp     adc temperature value to be converted
 * @param pressure     computed real pressure
 * @param temperature  computed real temperature
 */
static void MPL115_convert_adc_to_real(double   adc_pressure,
                                       double   adc_temp,
                                       double * pressure,
                                       double * temperature)
{
    double Pcomp;
    Pcomp = mpl115_coeffA0 +                                            \
        (mpl115_coeffB1 + mpl115_coeffC11*adc_pressure + mpl115_coeffC12*adc_temp) * adc_pressure + \
        (mpl115_coeffB2 + mpl115_coeffC22*adc_temp) * adc_temp;
    
    *pressure = ((1150.0-500.0) * Pcomp / 1023.0) + 500.0;
    *temperature = (472.0 - adc_temp) / 5.35 + 25.0;
    DEBUG ("barometer: convert_adc_to_real - got %lf hPa, %lf C",
           *pressure,
           *temperature);
}


/** 
 * Read sensor averegaed measurements
 *
 * @param pressure    averaged measured pressure
 * @param temperature averaged measured temperature
 *
 * @return Zero when successful
 */
static int MPL115_read_averaged(double * pressure, double * temperature)
{
    uint8_t mpl115_conv[MPL115_NUM_CONV]; 
    int8_t  res;
    int     retries;
    int     conv_pressure;
    int     conv_temperature;
    double  adc_pressure;
    double  adc_temperature;
    char    errbuf[1024];

    *pressure    = 0.0;
    *temperature = 0.0;
   
    /* start conversion of both temp and presure */
    retries = MPL115_CONVERSION_RETRIES;
    while (retries>0)
    {
        /* write 1 to start conversion */
        res = i2c_smbus_write_byte_data (i2c_bus_fd,
                                         MPL115_CMD_CONVERT_BOTH,
                                         0x01);
        if (res >= 0)
            break;

        --retries;
        if(retries>0)
        {
            ERROR ("barometer: MPL115_read_averaged - requesting conversion: %s, " \
                   "will retry at most %d more times",
                   sstrerror (errno, errbuf, sizeof (errbuf)),
                   retries);
        }
        else
        {
            ERROR ("barometer: MPL115_read_averaged - requesting conversion: %s, "\
                   "too many failed retries",
                   sstrerror (errno, errbuf, sizeof (errbuf)));
            return -1;
        }
    }

    usleep (10000); /* wait 10ms for the conversion */

    retries=MPL115_CONVERSION_RETRIES;
    while (retries>0)
    {
        res = i2c_smbus_read_i2c_block_data(i2c_bus_fd,
                                            MPL115_ADDR_CONV,
                                            MPL115_NUM_CONV,
                                            mpl115_conv); 
        if (res >= 0)
            break;

        --retries;
        if (retries>0)
        {
            ERROR ("barometer: MPL115_read_averaged - reading conversion: %s, " \
                   "will retry at most %d more times",
                   sstrerror (errno, errbuf, sizeof (errbuf)),
                   retries);
        }
        else
        {
            ERROR ("barometer: MPL115_read_averaged - reading conversion: %s, " \
                   "too many failed retries",
                   sstrerror (errno, errbuf, sizeof (errbuf)));
            return -1;
        }
    }
    
    conv_pressure    = ((mpl115_conv[0] << 8) | mpl115_conv[1]) >> 6;
    conv_temperature = ((mpl115_conv[2] << 8) | mpl115_conv[3]) >> 6;
    DEBUG ("barometer: MPL115_read_averaged, raw pressure ADC value = %d, " \
           "raw temperature ADC value = %d",
           conv_pressure,
           conv_temperature);

    adc_pressure    = averaging_add_sample (&pressure_averaging, conv_pressure);
    adc_temperature = averaging_add_sample (&temperature_averaging, conv_temperature);

    MPL115_convert_adc_to_real(adc_pressure, adc_temperature, pressure, temperature);

    DEBUG ("barometer: MPL115_read_averaged - averaged ADC pressure = %lf / temperature = %lf, " \
           "real pressure = %lf hPa / temperature = %lf C",
           adc_pressure,
           adc_temperature,
           *pressure,
           *temperature);
    
    return 0;
}

/* ------------------------ MPL3115 access ------------------------ */

/** 
 * Detect presence of a MPL3115 pressure sensor by checking register "WHO AM I"
 * 
 * @return 1 if MPL3115, 0 otherwise
 */
static int MPL3115_detect(void)
{
    __s32 res;

    res = i2c_smbus_read_byte_data(i2c_bus_fd, MPL3115_REG_WHO_AM_I);
    if(res == MPL3115_WHO_AM_I_RESP)
    {
        DEBUG ("barometer: MPL3115_detect - positive detection");
        return 1;
    }

    DEBUG ("barometer: MPL3115_detect - negative detection");
    return 0;
}

/** 
 * Adjusts oversampling to values supported by MPL3115
 *
 * MPL3115 supports only power of 2 in the range 1 to 128. 
 */
static void MPL3115_adjust_oversampling(void)
{
    int new_val = 0;

    if(config_oversample > 100)
    {
        new_val = 128;
        oversample_MPL3115 = MPL3115_CTRL_REG1_OST_128;
    }
    else if(config_oversample > 48)
    {
        new_val = 64;
        oversample_MPL3115 = MPL3115_CTRL_REG1_OST_64;
    }
    else if(config_oversample > 24)
    {
        new_val = 32;
        oversample_MPL3115 = MPL3115_CTRL_REG1_OST_32;
    }
    else if(config_oversample > 12)
    {
        new_val = 16;
        oversample_MPL3115 = MPL3115_CTRL_REG1_OST_16;
    }
    else if(config_oversample > 6)
    {
        new_val = 8;
        oversample_MPL3115 = MPL3115_CTRL_REG1_OST_8;
    }
    else if(config_oversample > 3)
    {
        new_val = 4;
        oversample_MPL3115 = MPL3115_CTRL_REG1_OST_4;
    }
    else if(config_oversample > 1)
    {
        new_val = 2;
        oversample_MPL3115 = MPL3115_CTRL_REG1_OST_2;
    }
    else
    {
        new_val = 1;
        oversample_MPL3115 = MPL3115_CTRL_REG1_OST_1;
    }

    DEBUG("barometer: correcting oversampling for MPL3115 from %d to %d",
          config_oversample, 
          new_val);
    config_oversample = new_val;
}

/** 
 * Read sensor averegaed measurements
 *
 * @param pressure    averaged measured pressure
 * @param temperature averaged measured temperature
 *
 * @return Zero when successful
 */
static int MPL3115_read(double * pressure, double * temperature)
{
    __s32 res;
    __s32 ctrl ;
    __u8 data[MPL3115_NUM_CONV_VALS];
    long int tmp_value = 0;
    char errbuf[1024];
    
    /* Set Active - activate the device from standby */
    res = i2c_smbus_read_byte_data(i2c_bus_fd, MPL3115_REG_CTRL_REG1);
    if (res < 0)
    {
        ERROR ("barometer: MPL3115_read - cannot read CTRL_REG1: %s",
               sstrerror (errno, errbuf, sizeof (errbuf)));
        return 1;
    }
    ctrl = res;
    res = i2c_smbus_write_byte_data(i2c_bus_fd, 
                                    MPL3115_REG_CTRL_REG1, 
                                    ctrl | MPL3115_CTRL_REG1_SBYB);
    if (res < 0)
    {
        ERROR ("barometer: MPL3115_read - problem activating: %s",
               sstrerror (errno, errbuf, sizeof (errbuf)));
        return 1;
    }
    
    /* base sleep is 5ms x OST */
    usleep(5000 * config_oversample);
      
    /* check the flags/status if ready */
    res = i2c_smbus_read_byte_data(i2c_bus_fd, MPL3115_REG_STATUS);
    if (res < 0)
    {
        ERROR ("barometer: MPL3115_read - cannot read status register: %s",
               sstrerror (errno, errbuf, sizeof (errbuf)));
        return 1;
    }
    
    while ((res & MPL3115_DR_STATUS_DR) != MPL3115_DR_STATUS_DR)
    {
        /* try some extra sleep... */
        usleep(10000);
        
        /* ... and repeat the check. The conversion has to finish sooner or later. */
        res = i2c_smbus_read_byte_data(i2c_bus_fd, MPL3115_REG_STATUS);
        if (res < 0)
        {
            ERROR ("barometer: MPL3115_read - cannot read status register: %s",
                   sstrerror (errno, errbuf, sizeof (errbuf)));
            return 1;
        }
    }
    
    /* Now read all the data in one block. There is address autoincrement. */
    res = i2c_smbus_read_i2c_block_data(i2c_bus_fd, 
                                        MPL3115_REG_OUT_P_MSB, 
                                        MPL3115_NUM_CONV_VALS,
                                        data);
    if (res < 0)
    {
        ERROR ("barometer: MPL3115_read - cannot read data registers: %s",
               sstrerror (errno, errbuf, sizeof (errbuf)));
        return 1;
    }
    
    tmp_value = (data[0] << 16) | (data[1] << 8) | data[2];
    *pressure = ((double) tmp_value) / 4.0 / 16.0 / 100.0;
    DEBUG ("barometer: MPL3115_read, absolute pressure = %lf hPa", *pressure);
    
    if(data[3] > 0x7F)
    {
        data[3] = ~data[3] + 1;
        *temperature = data[3];
        *temperature = - *temperature;
    }
    else
    {
        *temperature = data[3];
    }
    
    *temperature += (double)(data[4]) / 256.0;
    DEBUG ("barometer: MPL3115_read, temperature = %lf C", *temperature);
    
    return 0;
}

/** 
 * Initialize MPL3115 for barometeric measurements
 * 
 * @return 0 if successful
 */
static int MPL3115_init_sensor(void)
{
    __s32 res;
    __s8 offset;
    char errbuf[1024];
    
    /* Reset the sensor. It will reset immediately without ACKing */
    /* the transaction, so no error handling here. */
    i2c_smbus_write_byte_data(i2c_bus_fd, 
                              MPL3115_REG_CTRL_REG1, 
                              MPL3115_CTRL_REG1_RST);
    
    /* wait some time for the reset to finish */
    usleep(100000);

    /* now it should be in standby already so we can go and configure it */
    
    /*  Set temperature offset. */
    /*  result = ADCtemp + offset [C] */
    offset = (__s8) (config_temp_offset * 16.0);
    res = i2c_smbus_write_byte_data(i2c_bus_fd, MPL3115_REG_OFF_T, offset);
    if (res < 0)
    {
        ERROR ("barometer: MPL3115_init_sensor - problem setting temp offset: %s",
               sstrerror (errno, errbuf, sizeof (errbuf)));
        return -1;
    }
    
    /*  Set pressure offset. */
    /*  result = ADCpress + offset [hPa] */
    offset = (__s8) (config_press_offset * 100.0 / 4.0);
    res = i2c_smbus_write_byte_data(i2c_bus_fd, MPL3115_REG_OFF_P, offset);
    if (res < 0)
    {
        ERROR ("barometer: MPL3115_init_sensor - problem setting pressure offset: %s",
               sstrerror (errno, errbuf, sizeof (errbuf)));
        return -1;
    }

    /* Enable Data Flags in PT_DATA_CFG - flags on both pressure and temp */
    res = i2c_smbus_write_byte_data(i2c_bus_fd, 
                                    MPL3115_REG_PT_DATA_CFG,
                                    MPL3115_PT_DATA_DREM        \
                                    | MPL3115_PT_DATA_PDEF      \
                                    | MPL3115_PT_DATA_TDEF);
    if (res < 0)
    {
        ERROR ("barometer: MPL3115_init_sensor - problem setting PT_DATA_CFG: %s",
               sstrerror (errno, errbuf, sizeof (errbuf)));
        return -1;
    }

    /* Set to barometer with an OSR */ 
    res = i2c_smbus_write_byte_data(i2c_bus_fd, 
                                    MPL3115_REG_CTRL_REG1, 
                                    oversample_MPL3115);
    if (res < 0)
    {
        ERROR ("barometer: MPL3115_init_sensor - problem configuring CTRL_REG1: %s",
               sstrerror (errno, errbuf, sizeof (errbuf)));
        return -1;
    }

    return 0;
}


/* ------------------------ Common functionality ------------------------ */

/**
 * Convert absolute pressure (in hPa) to mean sea level pressure
 *
 * Implemented methods are:
 * - MSLP_NONE - no converions, returns absolute pressure
 *
 * - MSLP_INTERNATIONAL - see http://en.wikipedia.org/wiki/Atmospheric_pressure#Altitude_atmospheric_pressure_variation
 *           Requires #config_altitude
 *
 * - MSLP_DEU_WETT - formula as recommended by the Deutsche Wetterdienst. See 
 *                http://de.wikipedia.org/wiki/Barometrische_H%C3%B6henformel#Theorie
 *           Requires both #config_altitude and temperature reference(s).
 *
 * @param abs_pressure absloute pressure to be converted
 *
 * @return mean sea level pressure if successful, NAN otherwise
 */
static double abs_to_mean_sea_level_pressure(double abs_pressure)
{
    double mean = -1.0;
    double temp = 0.0;
    int result = 0;

    DEBUG ("barometer: abs_to_mean_sea_level_pressure: absPressure = %lf, method = %d",
           abs_pressure,
           config_normalize);

    if (config_normalize >= MSLP_DEU_WETT)
    {
        result = get_reference_temperature(&temp);
        if(result)
        {
            return NAN;
        }
    }

    switch(config_normalize)
    {
    case MSLP_NONE:
        mean = abs_pressure;
        break;
        
    case MSLP_INTERNATIONAL:
        mean = abs_pressure / \
            pow(1.0 - 0.0065*config_altitude/288.15, 0.0065*0.0289644/(8.31447*0.0065));
        break;
        
    case MSLP_DEU_WETT:
    {
        double E; /* humidity */
        double x;
        if(temp<9.1)
            E = 5.6402 * (-0.0916 + exp(0.06*temp) );
        else
            E = 18.2194 * (1.0463 - exp(-0.0666*temp) );
        x = 9.80665 / (287.05 * (temp+273.15 + 0.12*E + 0.0065*config_altitude/2)) * config_altitude;
        mean = abs_pressure * exp(x);
    }
    break;

    default:
        ERROR ("barometer: abs_to_mean_sea_level_pressure: wrong conversion method %d", 
               config_normalize);
        mean = abs_pressure;
        break;
    }

    return mean; 
}

/* ------------------------ main plugin callbacks ------------------------ */

/** 
 * Main plugin configuration callback (using simple config)
 * 
 * @param key   configuration key we should process
 * @param value configuration value we should process
 * 
 * @return Zero when successful.
 */
static int collectd_barometer_config (const char *key, const char *value)
{
    DEBUG("barometer: collectd_barometer_config");

    if (strcasecmp (key, "Device") == 0)
    {
        sfree (config_device);
        config_device = strdup (value);
    }
    else if (strcasecmp (key, "Oversampling") == 0)
    {
        int oversampling_tmp = atoi (value);
        if (oversampling_tmp < 1 || oversampling_tmp > 1024)
        {
            WARNING ("barometer: collectd_barometer_config: invalid oversampling: %d." \
                     " Allowed values are 1 to 1024 (for MPL115) or 128 (for MPL3115).",
                     oversampling_tmp);
            return 1;
        }
        config_oversample = oversampling_tmp;
    }
    else if (strcasecmp (key, "Altitude") == 0)
    {
        config_altitude = atof (value);
    }
    else if (strcasecmp (key, "Normalization") == 0)
    {
        int normalize_tmp = atoi (value);
        if (normalize_tmp < 0 || normalize_tmp > 2)
        {
            WARNING ("barometer: collectd_barometer_config: invalid normalization: %d",
                     normalize_tmp);
            return 1;
        }
        config_normalize = normalize_tmp;
    }
    else if (strcasecmp (key, "TemperatureSensor") == 0)
    {
        if(temp_list_add(temp_list, value))
        {
            return -1;
        }
    }
    else if (strcasecmp (key, "PressureOffset") == 0)
    {
        config_press_offset = atof(value);
    }
    else if (strcasecmp (key, "TemperatureOffset") == 0)
    {
        config_temp_offset = atof(value);
    }
    else 
    {
        return -1;
    }

    return 0;
}


/** 
 * Shutdown callback.
 * 
 * Close I2C and delete all the buffers.
 * 
 * @return Zero when successful (at the moment the only possible outcome)
 */
static int collectd_barometer_shutdown(void)
{
    DEBUG ("barometer: collectd_barometer_shutdown");

    if(!is_MPL3115)
    {
        averaging_delete (&pressure_averaging);
        averaging_delete (&temperature_averaging);

        temp_list_delete(&temp_list);
    }

    if (i2c_bus_fd > 0)
    {
        close (i2c_bus_fd);
        i2c_bus_fd = -1;
        sfree (config_device);
    }

    return 0;
}


/** 
 * Plugin read callback for MPL115.
 * 
 *  Dispatching will create values:
 *  - <hostname>/barometer-mpl115/pressure-normalized
 *  - <hostname>/barometer-mpl115/pressure-absolute
 *  - <hostname>/barometer-mpl115/temperature
 *
 * @return Zero when successful.
 */
static int MPL115_collectd_barometer_read (void)
{
    int result = 0;

    double pressure        = 0.0;
    double temperature     = 0.0;
    double norm_pressure   = 0.0;

    value_list_t vl = VALUE_LIST_INIT;
    value_t      values[1];
    
    DEBUG("barometer: MPL115_collectd_barometer_read");

    if (!configured)
    {
        return -1;
    }

    /* Rather than delaying init, we will intitialize during first read. This
       way at least we have a better chance to have the reference temperature
       already available. */
    if(!avg_initialized)
    {
        int i;
        for(i=0; i<config_oversample-1; ++i)
        {
            result = MPL115_read_averaged(&pressure, &temperature);
            if(result)
            {
                ERROR ("barometer: MPL115_collectd_barometer_read - mpl115 read, ignored during init");
            }
            DEBUG("barometer: MPL115_collectd_barometer_read - init %d / %d", i+1, config_oversample-1);
            usleep(20000);
        }
        avg_initialized = 1;
    }

    result = MPL115_read_averaged(&pressure, &temperature);
    if(result)
        return result;

    norm_pressure = abs_to_mean_sea_level_pressure(pressure);

    sstrncpy (vl.host, hostname_g, sizeof (vl.host));
    sstrncpy (vl.plugin, "barometer", sizeof (vl.plugin));
    sstrncpy (vl.plugin_instance, "mpl115", sizeof (vl.plugin_instance));

    vl.values_len = 1;
    vl.values = values;

    /* dispatch normalized air pressure */
    sstrncpy (vl.type, "pressure", sizeof (vl.type));
    sstrncpy (vl.type_instance, "normalized", sizeof (vl.type_instance));
    values[0].gauge = norm_pressure;
    plugin_dispatch_values (&vl);

    /* dispatch absolute air pressure */
    sstrncpy (vl.type, "pressure", sizeof (vl.type));
    sstrncpy (vl.type_instance, "absolute", sizeof (vl.type_instance));
    values[0].gauge = pressure;
    plugin_dispatch_values (&vl);

    /* dispatch sensor temperature */
    sstrncpy (vl.type, "temperature", sizeof (vl.type));
    sstrncpy (vl.type_instance, "", sizeof (vl.type_instance));
    values[0].gauge = temperature;
    plugin_dispatch_values (&vl);

    return 0;
}


/** 
 * Plugin read callback for MPL3115.
 * 
 *  Dispatching will create values:
 *  - <hostname>/barometer-mpl3115/pressure-normalized
 *  - <hostname>/barometer-mpl3115/pressure-absolute
 *  - <hostname>/barometer-mpl3115/temperature
 *
 * @return Zero when successful.
 */
static int MPL3115_collectd_barometer_read (void)
{
    int result = 0;
    
    double pressure        = 0.0;
    double temperature     = 0.0;
    double norm_pressure   = 0.0;
    
    value_list_t vl = VALUE_LIST_INIT;
    value_t      values[1];
    
    DEBUG("barometer: MPL3115_collectd_barometer_read");
    
    if (!configured)
    {
        return -1;
    }
    
    result = MPL3115_read(&pressure, &temperature);
    if(result)
        return result;

    norm_pressure = abs_to_mean_sea_level_pressure(pressure);

    sstrncpy (vl.host, hostname_g, sizeof (vl.host));
    sstrncpy (vl.plugin, "barometer", sizeof (vl.plugin));
    sstrncpy (vl.plugin_instance, "mpl3115", sizeof (vl.plugin_instance));

    vl.values_len = 1;
    vl.values = values;

    /* dispatch normalized air pressure */
    sstrncpy (vl.type, "pressure", sizeof (vl.type));
    sstrncpy (vl.type_instance, "normalized", sizeof (vl.type_instance));
    values[0].gauge = norm_pressure;
    plugin_dispatch_values (&vl);

    /* dispatch absolute air pressure */
    sstrncpy (vl.type, "pressure", sizeof (vl.type));
    sstrncpy (vl.type_instance, "absolute", sizeof (vl.type_instance));
    values[0].gauge = pressure;
    plugin_dispatch_values (&vl);

    /* dispatch sensor temperature */
    sstrncpy (vl.type, "temperature", sizeof (vl.type));
    sstrncpy (vl.type_instance, "", sizeof (vl.type_instance));
    values[0].gauge = temperature;
    plugin_dispatch_values (&vl);

    return 0;
}


/** 
 * Initialization callback
 * 
 * Check config, initialize I2C bus access, conversion coefficients and averaging
 * ring buffers
 * 
 * @return Zero when successful.
 */
static int collectd_barometer_init (void)
{
    char errbuf[1024];

    DEBUG ("barometer: collectd_barometer_init");

    if (config_device == NULL)
    {
        ERROR("barometer: collectd_barometer_init I2C bus device not configured");
        return -1;
    }

    if (config_normalize >= MSLP_INTERNATIONAL && isnan(config_altitude))
    {
        ERROR("barometer: collectd_barometer_init no altitude configured " \
              "for mean sea level pressure normalization.");
        return -1;
    }

    if (config_normalize == MSLP_DEU_WETT
        &&
        temp_list == NULL)
    {
        ERROR("barometer: collectd_barometer_init no temperature reference "\
              "configured for mean sea level pressure normalization.");
        return -1;
    }


    i2c_bus_fd = open(config_device, O_RDWR);
    if (i2c_bus_fd < 0)
    {
        ERROR ("barometer: collectd_barometer_init problem opening I2C bus device \"%s\": %s (is loaded mod i2c-dev?)",
               config_device,
               sstrerror (errno, errbuf, sizeof (errbuf)));
        return -1;
    }

    if (ioctl(i2c_bus_fd, I2C_SLAVE_FORCE, MPL115_I2C_ADDRESS) < 0)
    {
        ERROR("barometer: collectd_barometer_init problem setting i2c slave address to 0x%02X: %s",
              MPL115_I2C_ADDRESS,
              sstrerror (errno, errbuf, sizeof (errbuf)));
        return -1;
    }

    /* detect sensor type - MPL115 or MPL3115 */
    is_MPL3115 = MPL3115_detect();

    /* init correct sensor type */
    if(is_MPL3115) /* MPL3115 */
    {
        MPL3115_adjust_oversampling();

        if(MPL3115_init_sensor())
            return -1;

        plugin_register_read ("barometer", MPL3115_collectd_barometer_read);
    }
    else /* MPL115 */
    {
        if (averaging_create (&pressure_averaging, config_oversample))
        {
            ERROR("barometer: collectd_barometer_init pressure averaging init failed");
            return -1;
        }
        
        if (averaging_create (&temperature_averaging, config_oversample))
        {
            ERROR("barometer: collectd_barometer_init temperature averaging init failed");
            return -1;
        }
        
        if (MPL115_read_coeffs() < 0)
            return -1;

        plugin_register_read ("barometer", MPL115_collectd_barometer_read);
    }

    configured = 1;
    return 0;
}

/* ------------------------ plugin register / entry point ------------------------ */

/** 
 * Plugin "entry" - register all callback.
 * 
 */
void module_register (void)
{
    plugin_register_config ("barometer", 
                            collectd_barometer_config, 
                            config_keys, 
                            config_keys_num);
    plugin_register_init ("barometer", collectd_barometer_init);
    plugin_register_shutdown ("barometer", collectd_barometer_shutdown);
}
