package Collectd::Graph::Type::Df;

# Copyright (C) 2008,2009  Florian octo Forster <octo at verplant.org>
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
  return ([qw(free used)]);
} # getDataSources

sub new
{
  my $pkg = shift;
  my $obj = Collectd::Graph::Type->new (@_);
  $obj->{'data_sources'} = [qw(free used)];
  $obj->{'rrd_opts'} = ['-v', 'Bytes'];
  $obj->{'rrd_title'} = 'Disk space ({type_instance})';
  $obj->{'rrd_format'} = '%5.1lf%sB';
  $obj->{'colors'} = [qw(00b000 ff0000)];

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

  return (['-t', 'Diskspace (' . $ident->{'type_instance'} . ')', '-v', 'Bytes', '-l', '0',
    "DEF:free_min=${filename}:free:MIN",
    "DEF:free_avg=${filename}:free:AVERAGE",
    "DEF:free_max=${filename}:free:MAX",
    "DEF:used_min=${filename}:used:MIN",
    "DEF:used_avg=${filename}:used:AVERAGE",
    "DEF:used_max=${filename}:used:MAX",
    "CDEF:both_avg=free_avg,used_avg,+",
    "AREA:both_avg#${faded_green}",
    "AREA:used_avg#${faded_red}",
    'LINE1:both_avg#00ff00:Free',
    'GPRINT:free_min:MIN:%5.1lf%sB Min,',
    'GPRINT:free_avg:AVERAGE:%5.1lf%sB Avg,',
    'GPRINT:free_max:MAX:%5.1lf%sB Max,',
    'GPRINT:free_avg:LAST:%5.1lf%sB Last\l',
    'LINE1:used_avg#ff0000:Used',
    'GPRINT:used_min:MIN:%5.1lf%sB Min,',
    'GPRINT:used_avg:AVERAGE:%5.1lf%sB Avg,',
    'GPRINT:used_max:MAX:%5.1lf%sB Max,',
    'GPRINT:used_avg:LAST:%5.1lf%sB Last\l']);
} # getRRDArgs

# vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
