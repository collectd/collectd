/**
 * collectd - src/mbus_utils.c
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

#include "mbus_utils.h"
#include "common.h"
#include <string.h>

mbus_slave * mbus_slave_new(void)
{
    DEBUG("mbus: mbus_slave_new - creating new slave");
    mbus_slave * slave = (mbus_slave *) malloc(sizeof(mbus_slave));
    if(slave == NULL)
        return NULL;
    
    memset(slave, 0, sizeof(mbus_slave));
    slave->address.is_primary = 1;
    slave->address.secondary = NULL;
    slave->next_slave = NULL;
    return slave;
}

void mbus_slave_init_mask(mbus_slave * slave, _Bool clear)
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

void mbus_slave_free(mbus_slave * slave)
{
    DEBUG("mbus: mbus_slave_free - deleting slave");
    if(slave == NULL)
        return;
    
    if(!(slave->address.is_primary))
        sfree(slave->address.secondary);

    free(slave);
}

void mbus_slave_record_add(mbus_slave * slave, int record_number)
{
    int byte=record_number/8;
    int bit =record_number%8;

    DEBUG("mbus: mbus_slave_record_add - adding record %d", record_number);
    slave->mask[byte] |= 1 << bit;
}

void mbus_slave_record_remove(mbus_slave * slave, int record_number)
{
    int byte=record_number/8;
    int bit =record_number%8;

    DEBUG("mbus: mbus_slave_record_remove - removing record %d", record_number);
    slave->mask[byte] &= (0xff ^ (1<<bit));
}

int mbus_slave_record_check(mbus_slave * slave, int record_number)
{
    int byte=record_number/8;
    int bit =record_number%8;

    int result = (slave->mask[byte] & (1<<bit)) ? 1 : 0;
    DEBUG("mbus: mbus_slave_record_check - checking record %d with result %d", record_number, result);
    return result;
}
