package Collectd::Graph::Type::ArcCounts;

# Copyright (C) 2009  Anthony Dewhurst <dewhurst at gmail>
#
# This program is available software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the available Software
# Foundation; only version 2 of the License is applicable.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the available Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

use strict;
use warnings;
use base ('Collectd::Graph::Type');

use Collectd::Graph::Common (qw(ident_to_filename get_faded_color));

return (1);

sub getDataSources
{
  return ([qw(available usedbydatasetbydataset usedbysnapshots usedbyrefres usedbychildren)]);
} # getDataSources

sub new
{
  my $pkg = shift;
  my $obj = Collectd::Graph::Type->new (@_);
  $obj->{'data_sources'} = [qw(demand_data demand_metadata prefetch_data prefetch_metadata)];
  $obj->{'rrd_opts'} = [];
  $obj->{'rrd_title'} = 'ARC {type_instance} on {hostname}';

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

  my $legend = $ident->{'type_instance'};


  my $faded_green = get_faded_color ('00ff00');
  my $faded_blue = get_faded_color ('0000ff');
  my $faded_red = get_faded_color ('ff0000');
  my $faded_cyan = get_faded_color ('00ffff');

  my @ret = @{$obj->{'rrd_opts'}};

  push @ret, '-t', $obj->getTitle($ident);
  push @ret, '-v', ucfirst($ident->{'type_instance'});

  my $ds = {
    demand_data       => { legend => "Demand data    ", color => "00ff00" },
    demand_metadata   => { legend => "Demand metadata", color => "0000ff" },
    prefetch_data     => { legend => "Prefetch data  ", color => "ff0000" },
    prefetch_metadata => { legend => "Prefetch meta  ", color => "ff00ff" },
  };

  foreach (qw(demand_data demand_metadata prefetch_data prefetch_metadata))
  {
    push @ret,
      "DEF:${_}_min=${filename}:${_}:MIN",
      "DEF:${_}_avg=${filename}:${_}:AVERAGE",
      "DEF:${_}_max=${filename}:${_}:MAX";
  }

  {
    push @ret,
      "CDEF:stack_prefetch_metadata=prefetch_metadata_avg",
      "CDEF:stack_prefetch_data=prefetch_data_avg,stack_prefetch_metadata,+",
      "CDEF:stack_demand_metadata=demand_metadata_avg,stack_prefetch_data,+",
      "CDEF:stack_demand_data=demand_data_avg,stack_demand_metadata,+",
      "AREA:stack_demand_data#${faded_green}",
      "AREA:stack_demand_metadata#${faded_blue}",
      "AREA:stack_prefetch_data#${faded_red}",
      "AREA:stack_prefetch_metadata#${faded_cyan}",
  }

  foreach (qw(demand_data demand_metadata prefetch_data prefetch_metadata))
  {
    push @ret,
      "LINE1:stack_${_}#" . $ds->{$_}->{color} . ":" . $ds->{$_}->{legend},
      "GPRINT:${_}_min:MIN:%5.1lf Min,",
      "GPRINT:${_}_avg:AVERAGE:%5.1lf Avg,",
      "GPRINT:${_}_max:MAX:%5.1lf Max,",
      "GPRINT:${_}_avg:LAST:%5.1lf Last\l";
  }

  return \@ret;

} # getRRDArgs

# vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
