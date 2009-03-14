package Collectd::Graph::Type::PsCputime;

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
  return ([qw(syst user)]);
} # getDataSources

sub new
{
  my $pkg = shift;
  my $obj = Collectd::Graph::Type->new (@_);
  $obj->{'data_sources'} = [qw(syst user)];
  $obj->{'rrd_opts'} = ['-v', 'CPU time [s]', '-l', '0'];
  $obj->{'rrd_title'} = 'CPU time used by {plugin_instance}';
  $obj->{'rrd_format'} = '%5.1lf';
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

  my $faded_blue = get_faded_color ('0000ff');
  my $faded_red = get_faded_color ('ff0000');

  return (['-t', $obj->getTitle (), @{$obj->{'rrd_opts'}},
    "DEF:user_min_raw=${filename}:user:MIN",
    "DEF:user_avg_raw=${filename}:user:AVERAGE",
    "DEF:user_max_raw=${filename}:user:MAX",
    "DEF:syst_min_raw=${filename}:syst:MIN",
    "DEF:syst_avg_raw=${filename}:syst:AVERAGE",
    "DEF:syst_max_raw=${filename}:syst:MAX",
    "CDEF:user_min=0.000001,user_min_raw,*",
    "CDEF:user_avg=0.000001,user_avg_raw,*",
    "CDEF:user_max=0.000001,user_max_raw,*",
    "CDEF:syst_min=0.000001,syst_min_raw,*",
    "CDEF:syst_avg=0.000001,syst_avg_raw,*",
    "CDEF:syst_max=0.000001,syst_max_raw,*",
    "VDEF:v_user_min=user_min,MINIMUM",
    "VDEF:v_user_avg=user_avg,AVERAGE",
    "VDEF:v_user_max=user_max,MAXIMUM",
    "VDEF:v_user_lst=syst_avg,LAST",
    "VDEF:v_syst_min=syst_min,MINIMUM",
    "VDEF:v_syst_avg=syst_avg,AVERAGE",
    "VDEF:v_syst_max=syst_max,MAXIMUM",
    "VDEF:v_syst_lst=syst_avg,LAST",
    "CDEF:user_stack=syst_avg,user_avg,+",
    "AREA:user_stack#${faded_blue}",
    'LINE1:user_stack#0000ff:User  ',
    'GPRINT:v_user_min:%5.1lf%ss Min,',
    'GPRINT:v_user_avg:%5.1lf%ss Avg,',
    'GPRINT:v_user_max:%5.1lf%ss Max,',
    'GPRINT:v_user_lst:%5.1lf%ss Last\l',
    "AREA:syst_avg#${faded_red}",
    'LINE1:syst_avg#ff0000:System',
    'GPRINT:v_syst_min:%5.1lf%ss Min,',
    'GPRINT:v_syst_avg:%5.1lf%ss Avg,',
    'GPRINT:v_syst_max:%5.1lf%ss Max,',
    'GPRINT:v_syst_lst:%5.1lf%ss Last\l']);
} # getRRDArgs

# vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
