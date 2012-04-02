/**
 * collectd - src/mbus_utils.h
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

#ifndef __MBUS_UTILS_H__
#define __MBUS_UTILS_H__

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

/** 
 * Allocates new structure repreesnting an MBus slave
 * 
 * @return created slave if successful, NULL otherwise
 */
mbus_slave * mbus_slave_new(void);

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
void mbus_slave_init_mask(mbus_slave * slave, _Bool clear);

/** 
 * Free previously created slave structure
 * 
 * @param slave Existing MBus slave structure
 */
void mbus_slave_free(mbus_slave * slave);


/** 
 * Adds (enables for processing) given record
 * 
 * @param slave         Existing MBus slave structure
 * @param record_number Zero based record number/index to be added/enabled
 */
void mbus_slave_record_add(mbus_slave * slave, int record_number);

/** 
 * Removes (disables for processing) given record
 * 
 * @param slave         Existing MBus slave structure
 * @param record_number Zero based record number/index to be removed/disabled
 */
void mbus_slave_record_remove(mbus_slave * slave, int record_number);

/** 
 * Checks whether given record is enabled to be processed
 * 
 * @param slave         Existing MBus slave structure
 * @param record_number Zero based record number/index to be checked
 * 
 * @return One if enabled, zero when disabled
 */
int  mbus_slave_record_check(mbus_slave * slave, int record_number);

#endif /* __MBUS_UTILS_H__ */
