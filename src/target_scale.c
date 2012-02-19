/**
 * collectd - src/target_scale.c
 * Copyright (C) 2008-2009  Florian Forster
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
 * Authors:
 *   Florian Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "filter_chain.h"

#include "utils_cache.h"

struct ts_data_s
{
	double factor;
	double offset;

	char **data_sources;
	size_t data_sources_num;
};
typedef struct ts_data_s ts_data_t;

static int ts_invoke_counter (const data_set_t *ds, value_list_t *vl, /* {{{ */
		ts_data_t *data, int dsrc_index)
{
	uint64_t curr_counter;
	int status;
	int failure;

	/* Required meta data */
	uint64_t prev_counter;
	char key_prev_counter[128];
	uint64_t int_counter;
	char key_int_counter[128];
	double int_fraction;
	char key_int_fraction[128];

	curr_counter = (uint64_t) vl->values[dsrc_index].counter;

	ssnprintf (key_prev_counter, sizeof (key_prev_counter),
			"target_scale[%p,%i]:prev_counter",
			(void *) data, dsrc_index);
	ssnprintf (key_int_counter, sizeof (key_int_counter),
			"target_scale[%p,%i]:int_counter",
			(void *) data, dsrc_index);
	ssnprintf (key_int_fraction, sizeof (key_int_fraction),
			"target_scale[%p,%i]:int_fraction",
			(void *) data, dsrc_index);

	prev_counter = curr_counter;
	int_counter = 0;
	int_fraction = 0.0;

	/* Query the meta data */
	failure = 0;

	status = uc_meta_data_get_unsigned_int (vl, key_prev_counter,
			&prev_counter);
	if (status != 0)
		failure++;

	status = uc_meta_data_get_unsigned_int (vl, key_int_counter, &int_counter);
	if (status != 0)
		failure++;

	status = uc_meta_data_get_double (vl, key_int_fraction, &int_fraction);
	if (status != 0)
		failure++;

	if (failure == 0)
	{
		uint64_t difference;
		double rate;

		/* Calcualte the rate */
		if (prev_counter > curr_counter) /* => counter overflow */
		{
			if (prev_counter <= 4294967295UL) /* 32 bit overflow */
				difference = (4294967295UL - prev_counter) + curr_counter;
			else /* 64 bit overflow */
				difference = (18446744073709551615ULL - prev_counter) + curr_counter;
		}
		else /* no overflow */
		{
			difference = curr_counter - prev_counter;
		}
		rate = ((double) difference) / CDTIME_T_TO_DOUBLE (vl->interval);

		/* Modify the rate. */
		if (!isnan (data->factor))
			rate *= data->factor;
		if (!isnan (data->offset))
			rate += data->offset;

		/* Calculate the internal counter. */
		int_fraction += (rate * CDTIME_T_TO_DOUBLE (vl->interval));
		difference = (uint64_t) int_fraction;
		int_fraction -= ((double) difference);
		int_counter  += difference;

		assert (int_fraction >= 0.0);
		assert (int_fraction <  1.0);

		DEBUG ("Target `scale': ts_invoke_counter: %"PRIu64" -> %g -> %"PRIu64
				"(+%g)",
				curr_counter, rate, int_counter, int_fraction);
	}
	else /* (failure != 0) */
	{
		int_counter = 0;
		int_fraction = 0.0;
	}

	vl->values[dsrc_index].counter = (counter_t) int_counter;

	/* Update to the new counter value */
	uc_meta_data_add_unsigned_int (vl, key_prev_counter, curr_counter);
	uc_meta_data_add_unsigned_int (vl, key_int_counter, int_counter);
	uc_meta_data_add_double (vl, key_int_fraction, int_fraction);


	return (0);
} /* }}} int ts_invoke_counter */

static int ts_invoke_gauge (const data_set_t *ds, value_list_t *vl, /* {{{ */
		ts_data_t *data, int dsrc_index)
{
	if (!isnan (data->factor))
		vl->values[dsrc_index].gauge *= data->factor;
	if (!isnan (data->offset))
		vl->values[dsrc_index].gauge += data->offset;

	return (0);
} /* }}} int ts_invoke_gauge */

static int ts_invoke_derive (const data_set_t *ds, value_list_t *vl, /* {{{ */
		ts_data_t *data, int dsrc_index)
{
	int64_t curr_derive;
	int status;
	int failure;

	/* Required meta data */
	int64_t prev_derive;
	char key_prev_derive[128];
	int64_t int_derive;
	char key_int_derive[128];
	double int_fraction;
	char key_int_fraction[128];

	curr_derive = (int64_t) vl->values[dsrc_index].derive;

	ssnprintf (key_prev_derive, sizeof (key_prev_derive),
			"target_scale[%p,%i]:prev_derive",
			(void *) data, dsrc_index);
	ssnprintf (key_int_derive, sizeof (key_int_derive),
			"target_scale[%p,%i]:int_derive",
			(void *) data, dsrc_index);
	ssnprintf (key_int_fraction, sizeof (key_int_fraction),
			"target_scale[%p,%i]:int_fraction",
			(void *) data, dsrc_index);

	prev_derive = curr_derive;
	int_derive = 0;
	int_fraction = 0.0;

	/* Query the meta data */
	failure = 0;

	status = uc_meta_data_get_signed_int (vl, key_prev_derive,
			&prev_derive);
	if (status != 0)
		failure++;

	status = uc_meta_data_get_signed_int (vl, key_int_derive, &int_derive);
	if (status != 0)
		failure++;

	status = uc_meta_data_get_double (vl, key_int_fraction, &int_fraction);
	if (status != 0)
		failure++;

	if (failure == 0)
	{
		int64_t difference;
		double rate;

		/* Calcualte the rate */
		difference = curr_derive - prev_derive;
		rate = ((double) difference) / CDTIME_T_TO_DOUBLE (vl->interval);

		/* Modify the rate. */
		if (!isnan (data->factor))
			rate *= data->factor;
		if (!isnan (data->offset))
			rate += data->offset;

		/* Calculate the internal derive. */
		int_fraction += (rate * CDTIME_T_TO_DOUBLE (vl->interval));
		if (int_fraction < 0.0) /* handle negative integer rounding correctly */
			difference = ((int64_t) int_fraction) - 1;
		else
			difference = (int64_t) int_fraction;
		int_fraction -= ((double) difference);
		int_derive  += difference;

		assert (int_fraction >= 0.0);
		assert (int_fraction <  1.0);

		DEBUG ("Target `scale': ts_invoke_derive: %"PRIu64" -> %g -> %"PRIu64
				"(+%g)",
				curr_derive, rate, int_derive, int_fraction);
	}
	else /* (failure != 0) */
	{
		int_derive = 0;
		int_fraction = 0.0;
	}

	vl->values[dsrc_index].derive = (derive_t) int_derive;

	/* Update to the new derive value */
	uc_meta_data_add_signed_int (vl, key_prev_derive, curr_derive);
	uc_meta_data_add_signed_int (vl, key_int_derive, int_derive);
	uc_meta_data_add_double (vl, key_int_fraction, int_fraction);

	return (0);
} /* }}} int ts_invoke_derive */

static int ts_invoke_absolute (const data_set_t *ds, value_list_t *vl, /* {{{ */
		ts_data_t *data, int dsrc_index)
{
	uint64_t curr_absolute;
	double rate;
	int status;

	/* Required meta data */
	double int_fraction;
	char key_int_fraction[128];

	curr_absolute = (uint64_t) vl->values[dsrc_index].absolute;

	ssnprintf (key_int_fraction, sizeof (key_int_fraction),
			"target_scale[%p,%i]:int_fraction",
			(void *) data, dsrc_index);

	int_fraction = 0.0;

	/* Query the meta data */
	status = uc_meta_data_get_double (vl, key_int_fraction, &int_fraction);
	if (status != 0)
		int_fraction = 0.0;

	rate = ((double) curr_absolute) / CDTIME_T_TO_DOUBLE (vl->interval);

	/* Modify the rate. */
	if (!isnan (data->factor))
		rate *= data->factor;
	if (!isnan (data->offset))
		rate += data->offset;

	/* Calculate the new absolute. */
	int_fraction += (rate * CDTIME_T_TO_DOUBLE (vl->interval));
	curr_absolute = (uint64_t) int_fraction;
	int_fraction -= ((double) curr_absolute);

	vl->values[dsrc_index].absolute = (absolute_t) curr_absolute;

	/* Update to the new absolute value */
	uc_meta_data_add_double (vl, key_int_fraction, int_fraction);

	return (0);
} /* }}} int ts_invoke_absolute */

static int ts_config_set_double (double *ret, oconfig_item_t *ci) /* {{{ */
{
	if ((ci->values_num != 1)
			|| (ci->values[0].type != OCONFIG_TYPE_NUMBER))
	{
		WARNING ("scale target: The `%s' config option needs "
				"exactly one numeric argument.", ci->key);
		return (-1);
	}

	*ret = ci->values[0].value.number;
	DEBUG ("ts_config_set_double: *ret = %g", *ret);

	return (0);
} /* }}} int ts_config_set_double */

static int ts_config_add_data_source(ts_data_t *data, /* {{{ */
		oconfig_item_t *ci)
{
	size_t new_data_sources_num;
	char **temp;
	int i;

	/* Check number of arbuments. */
	if (ci->values_num < 1)
	{
		ERROR ("`value' match: `%s' needs at least one argument.",
				ci->key);
		return (-1);
	}

	/* Check type of arguments */
	for (i = 0; i < ci->values_num; i++)
	{
		if (ci->values[i].type == OCONFIG_TYPE_STRING)
			continue;

		ERROR ("`value' match: `%s' accepts only string arguments "
				"(argument %i is a %s).",
				ci->key, i + 1,
				(ci->values[i].type == OCONFIG_TYPE_BOOLEAN)
				? "truth value" : "number");
		return (-1);
	}

	/* Allocate space for the char pointers */
	new_data_sources_num = data->data_sources_num + ((size_t) ci->values_num);
	temp = (char **) realloc (data->data_sources,
			new_data_sources_num * sizeof (char *));
	if (temp == NULL)
	{
		ERROR ("`value' match: realloc failed.");
		return (-1);
	}
	data->data_sources = temp;

	/* Copy the strings, allocating memory as needed.  */
	for (i = 0; i < ci->values_num; i++)
	{
		size_t j;

		/* If we get here, there better be memory for us to write to.  */
		assert (data->data_sources_num < new_data_sources_num);

		j = data->data_sources_num;
		data->data_sources[j] = sstrdup (ci->values[i].value.string);
		if (data->data_sources[j] == NULL)
		{
			ERROR ("`value' match: sstrdup failed.");
			continue;
		}
		data->data_sources_num++;
	}

	return (0);
} /* }}} int ts_config_add_data_source */

static int ts_destroy (void **user_data) /* {{{ */
{
	ts_data_t *data;

	if (user_data == NULL)
		return (-EINVAL);

	data = (ts_data_t *) *user_data;

	if ((data != NULL) && (data->data_sources != NULL))
	{
		size_t i;
		for (i = 0; i < data->data_sources_num; i++)
			sfree (data->data_sources[i]);
		sfree (data->data_sources);
	}

	sfree (data);
	*user_data = NULL;

	return (0);
} /* }}} int ts_destroy */

static int ts_create (const oconfig_item_t *ci, void **user_data) /* {{{ */
{
	ts_data_t *data;
	int status;
	int i;

	data = (ts_data_t *) malloc (sizeof (*data));
	if (data == NULL)
	{
		ERROR ("ts_create: malloc failed.");
		return (-ENOMEM);
	}
	memset (data, 0, sizeof (*data));

	data->factor = NAN;
	data->offset = NAN;

	status = 0;
	for (i = 0; i < ci->children_num; i++)
	{
		oconfig_item_t *child = ci->children + i;

		if (strcasecmp ("Factor", child->key) == 0)
				status = ts_config_set_double (&data->factor, child);
		else if (strcasecmp ("Offset", child->key) == 0)
				status = ts_config_set_double (&data->offset, child);
		else if (strcasecmp ("DataSource", child->key) == 0)
				status = ts_config_add_data_source(data, child);
		else
		{
			ERROR ("Target `scale': The `%s' configuration option is not understood "
					"and will be ignored.", child->key);
			status = 0;
		}

		if (status != 0)
			break;
	}

	/* Additional sanity-checking */
	while (status == 0)
	{
		if (isnan (data->factor) && isnan (data->offset))
		{
			ERROR ("Target `scale': You need to at least set either the `Factor' "
					"or `Offset' option!");
			status = -1;
		}

		break;
	}

	if (status != 0)
	{
		ts_destroy ((void *) &data);
		return (status);
	}

	*user_data = data;
	return (0);
} /* }}} int ts_create */

static int ts_invoke (const data_set_t *ds, value_list_t *vl, /* {{{ */
		notification_meta_t __attribute__((unused)) **meta, void **user_data)
{
	ts_data_t *data;
	int i;

	if ((ds == NULL) || (vl == NULL) || (user_data == NULL))
		return (-EINVAL);

	data = *user_data;
	if (data == NULL)
	{
		ERROR ("Target `scale': Invoke: `data' is NULL.");
		return (-EINVAL);
	}

	for (i = 0; i < ds->ds_num; i++)
	{
		/* If we've got a list of data sources, is it in the list? */
		if (data->data_sources) {
			size_t j;
			for (j = 0; j < data->data_sources_num; j++)
				if (strcasecmp(ds->ds[i].name, data->data_sources[j]) == 0)
					break;

			/* No match, ignore */
			if (j >= data->data_sources_num)
				continue;
		}

		if (ds->ds[i].type == DS_TYPE_COUNTER)
			ts_invoke_counter (ds, vl, data, i);
		else if (ds->ds[i].type == DS_TYPE_GAUGE)
			ts_invoke_gauge (ds, vl, data, i);
		else if (ds->ds[i].type == DS_TYPE_DERIVE)
			ts_invoke_derive (ds, vl, data, i);
		else if (ds->ds[i].type == DS_TYPE_ABSOLUTE)
			ts_invoke_absolute (ds, vl, data, i);
		else
			ERROR ("Target `scale': Ignoring unknown data source type %i",
					ds->ds[i].type);
	}

	return (FC_TARGET_CONTINUE);
} /* }}} int ts_invoke */

void module_register (void)
{
	target_proc_t tproc;

	memset (&tproc, 0, sizeof (tproc));
	tproc.create  = ts_create;
	tproc.destroy = ts_destroy;
	tproc.invoke  = ts_invoke;
	fc_register_target ("scale", tproc);
} /* module_register */

/* vim: set sw=2 ts=2 tw=78 fdm=marker : */

