# collectd - Collectd.pm
# Copyright (C) 2007  Sebastian Harl
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; only version 2 of the License is applicable.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
#
# Author:
#   Sebastian Harl <sh at tokkee.org>

package Collectd;

use strict;
use warnings;

require Exporter;

our @ISA = qw( Exporter );

our %EXPORT_TAGS = (
	'funcs'    => [ qw( plugin_register plugin_unregister
	                    plugin_dispatch_values plugin_log ) ],
	'types'    => [ qw( TYPE_INIT TYPE_READ TYPE_WRITE TYPE_SHUTDOWN TYPE_LOG
	                    TYPE_DATASET ) ],
	'ds_types' => [ qw( DS_TYPE_COUNTER DS_TYPE_GAUGE ) ],
	'log'      => [ qw( LOG_ERR LOG_WARNING LOG_NOTICE LOG_INFO LOG_DEBUG ) ],
);

{
	my %seen;

	push @{$EXPORT_TAGS{'all'}}, grep {! $seen{$_}++ } @{$EXPORT_TAGS{$_}}
		foreach keys %EXPORT_TAGS;
}

Exporter::export_ok_tags('all');

bootstrap Collectd "4.1.4";

1;

# vim: set sw=4 ts=4 tw=78 noexpandtab :

