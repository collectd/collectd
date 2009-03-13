package Collectd::Graph::Type::Wirkleistung;

# Copyright (C) 2009  Stefan Pfab <spfab at noris.net>
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; only version 2 of the License is applicable.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

use strict;
use warnings;
use base ('Collectd::Graph::Type');

use Collectd::Graph::Common (qw(ident_to_filename get_faded_color));

return (1);

sub getDataSources
{
  return ([qw(wirkleistung)]);
} # getDataSources

sub new
{
  my $pkg = shift;
  my $obj = Collectd::Graph::Type->new (@_);
  $obj->{'data_sources'} = [qw(wirkleistung)];
  $obj->{'rrd_opts'} = ['-v', 'Watt'];
  $obj->{'rrd_title'} = 'Wirkleistung ({type_instance})';
  $obj->{'rrd_format'} = '%5.1lf%s W';
  $obj->{'colors'} = [qw(000000 f00000)];

  return (bless ($obj, $pkg));
} # new

sub getRRDArgs
{
  my $obj = shift;
  my $index = shift;

  my $ident = $obj->{'files'}[$index];
  if (!$ident)
  {
    cluck ("Invalid index: $index");
    return;
  }
  my $filename = ident_to_filename ($ident);
  $filename =~ s#:#\\:#g;

  my $faded_green = get_faded_color ('00ff00');
  my $faded_red = get_faded_color ('ff0000');

  return (['-t', 'Wirkleistung (' . $ident->{'type_instance'} . ')', '-v', 'Watt', '-l', '0',
    "DEF:min0=${filename}:kWh:MIN",
    "DEF:avg0=${filename}:kWh:AVERAGE",
    "DEF:max0=${filename}:kWh:MAX",
    'AREA:max0#bfbfbf',
    'AREA:min0#FFFFFF',
    'CDEF:watt_avg0=avg0,36000,*,',
    'CDEF:watt_min0=min0,36000,*,',
    'CDEF:watt_max0=max0,36000,*,',
    'CDEF:watt_total=avg0,10,*,',
    'VDEF:total=watt_total,TOTAL',
    'VDEF:first=watt_total,FIRST',
    'VDEF:last=watt_total,LAST',
    #'CDEF:first_value=first,POP',
    #'CDEF:first_time=first,POP',
    'LINE1:watt_avg0#000000:W',
    'HRULE:190#ff0000',
    'GPRINT:watt_min0:MIN:%4.1lfW Min,',
    'GPRINT:watt_avg0:AVERAGE:%4.1lfW Avg,',
    'GPRINT:watt_max0:MAX:%4.1lfW Max,',
    'GPRINT:watt_avg0:LAST:%4.1lfW Last\l',
    'GPRINT:total:%4.1lf%sWh Gesamtverbrauch im angezeigten Zeitraum\l',
    'GPRINT:first:erster Wert %c:strftime',
    'GPRINT:last:letzter Wert %c:strftime']);

    # HRULE:190\ ff0000    

} # getRRDArgs

# vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
