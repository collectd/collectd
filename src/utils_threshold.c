/**
 * collectd - src/utils_threshold.c
 * Copyright (C) 2014 Pierre-Yves Ritschard
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
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
 * Author:
 *   Pierre-Yves Ritschard <pyr at spootnik.org>
 **/

#include "collectd.h"
#include "common.h"
#include "utils_avltree.h"
#include "utils_threshold.h"

#include <pthread.h>

/*
 * Exported symbols
 * {{{ */
c_avl_tree_t   *threshold_tree = NULL;
pthread_mutex_t threshold_lock = PTHREAD_MUTEX_INITIALIZER;
/* }}} */
