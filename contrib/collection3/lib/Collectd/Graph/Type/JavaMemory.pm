package Collectd::Graph::Type::JavaMemory;

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

use Collectd::Graph::Common (qw($ColorCanvas $ColorFullBlue $ColorHalfBlue
  group_files_by_plugin_instance ident_to_filename sanitize_type_instance
  get_faded_color sort_idents_by_type_instance));

return (1);

sub getGraphsNum
{
  my $obj = shift;
  my $group = group_files_by_plugin_instance (@{$obj->{'files'}});

  return (scalar (keys %$group));
}

sub getRRDArgs
{
  my $obj = shift;
  my $index = shift;

  my $group = group_files_by_plugin_instance (@{$obj->{'files'}});
  my @group = sort (keys %$group);

  my $rrd_opts = $obj->{'rrd_opts'} || [];
  my $format = $obj->{'rrd_format'} || '%5.1lf';

  my $idents = $group->{$group[$index]};
  my %type_instance = ();

  my $ds = $obj->getDataSources ();
  if (!$ds)
  {
    confess ("obj->getDataSources failed.");
  }
  if (@$ds != 1)
  {
    confess ("I can only work with RRD files that have "
      . "exactly one data source!");
  }
  my $data_source = $ds->[0];

  my $rrd_title = $idents->[0]{'plugin_instance'};
  $rrd_title =~ s/^memory_pool-//;
  $rrd_title = "Memory pool \"$rrd_title\"";

  my $ds_names =
  {
    max       => 'Max      ',
    committed => 'Committed',
    used      => 'Used     ',
    init      => 'Init     '
  };

  my $colors =
  {
    max       => '00ff00',
    committed => 'ff8000',
    used      => 'ff0000',
    init      => '0000f0',
    'head-committed'    => '000000',
    'head-init'         => '000000',
    'head-max'          => '000000',
    'head-used'         => '000000',
    'nonhead-committed' => '000000',
    'nonhead-init'      => '000000',
    'nonhead-max'       => '000000',
    'nonhead-used'      => '000000'
  };
  my @ret = ('-t', $rrd_title, @$rrd_opts);

  if (defined $obj->{'rrd_vertical'})
  {
    push (@ret, '-v', $obj->{'rrd_vertical'});
  }
  else
  {
    push (@ret, '-v', "Bytes");
  }

  for (@$idents)
  {
    my $ident = $_;
    if ($ident->{'type_instance'})
    {
      $type_instance{$ident->{'type_instance'}} = $ident;
    }
  }

  if (exists ($type_instance{'committed'})
    && exists ($type_instance{'init'})
    && exists ($type_instance{'max'})
    && exists ($type_instance{'used'}))
  {
    for (qw(max committed init used))
    {
      my $inst = $_;
      my $file = ident_to_filename ($type_instance{$inst});
      my $color = $colors->{$inst};
      my $name = $ds_names->{$inst};
      push (@ret,
	"DEF:${inst}_min=${file}:value:MIN",
	"DEF:${inst}_avg=${file}:value:AVERAGE",
	"DEF:${inst}_max=${file}:value:MAX",
	"AREA:${inst}_avg#${color}10",
	"LINE1:${inst}_avg#${color}:${name}",
	"GPRINT:${inst}_min:MIN:%5.1lf\%sB Min,",
	"GPRINT:${inst}_avg:AVERAGE:%5.1lf\%sB Avg,",
	"GPRINT:${inst}_max:MAX:%5.1lf\%sB Max,",
	"GPRINT:${inst}_avg:LAST:%5.1lf\%sB Last\\l");
    }
    return (\@ret);
  }
  else
  {
    require Collectd::Graph::Type::GenericStacked;
    return (Collectd::Graph::Type::GenericStacked::getRRDArgs ($obj, $index));
  }
} # getRRDArgs

sub getGraphArgs
{
  my $obj = shift;
  my $index = shift;

  my $group = group_files_by_plugin_instance (@{$obj->{'files'}});
  my @group = sort (keys %$group);

  my $idents = $group->{$group[$index]};

  my @args = ();
  for (qw(hostname plugin plugin_instance type))
  {
    if (defined ($idents->[0]{$_}))
    {
      push (@args, $_ . '=' . $idents->[0]{$_});
    }
  }

  return (join (';', @args));
} # getGraphArgs

# vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
