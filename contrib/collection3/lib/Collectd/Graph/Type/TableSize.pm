package Collectd::Graph::Type::TableSize;

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
  ident_to_filename sanitize_type_instance
  get_random_color sort_idents_by_type_instance));

use RRDs ();

return (1);

sub _get_last_value
{
  my $ident = shift;
  my $value = undef;
  my $filename = ident_to_filename ($ident);
  my ($start ,$step ,$names ,$data) = RRDs::fetch ($filename, 'AVERAGE', '-s', '-3600');
  if (my $errmsg = RRDs::error ())
  {
    print STDERR "RRDs::fetch ($filename) failed: $errmsg\n";
    return;
  }

  for (@$data)
  {
    my $line = $_;

    for (@$line)
    {
      my $ds = $_;

      if (defined ($ds))
      {
	$value = $ds;
      }
    }
  } # for (@$data)

  return ($value);
} # _get_last_value

sub getLastValues
{
  my $obj = shift;
  my $last_value = {};

  if (exists ($obj->{'last_value'}))
  {
    return ($obj->{'last_value'});
  }

  for (@{$obj->{'files'}})
  {
    my $file = $_;

    $last_value->{$file} = _get_last_value ($file);
  }
  $obj->{'last_value'} = $last_value;
  return ($obj->{'last_value'});
} # getLastValues

sub _group_files
{
  my $obj = shift;
  my $data = [];
  my @files;
  my $last_values;

  $last_values = $obj->getLastValues ();

  @files = sort
  { 
    if (!defined ($last_values->{$a}) && !defined ($last_values->{$b}))
    {
      return (0);
    }
    elsif (!defined ($last_values->{$a}))
    {
      return (1);
    }
    elsif (!defined ($last_values->{$b}))
    {
      return (-1);
    }
    else
    {
      return ($last_values->{$a} <=> $last_values->{$b});
    }
  } (@{$obj->{'files'}});

  for (my $i = 0; $i < @files; $i++)
  {
    my $file = $files[$i];
    my $j = int ($i / 10);

    $data->[$j] ||= [];
    push (@{$data->[$j]}, $file);
  }

  return ($data);
} # _group_files

sub getGraphsNum
{
  my $obj = shift;
  my $group = _group_files ($obj);

  return (0 + @$group);
}

sub getRRDArgs
{
  my $obj = shift;
  my $index = shift;

  my $group = _group_files ($obj);

  my $rrd_opts = $obj->{'rrd_opts'} || [];
  my $format = $obj->{'rrd_format'} || '%5.1lf';

  my $idents = $group->[$index];
  my $ds_name_len = 0;

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

  my $rrd_title = $obj->getTitle ($idents->[0]);

  my $colors = $obj->{'rrd_colors'} || {};
  my @ret = ('-t', $rrd_title, @$rrd_opts);

  if (defined $obj->{'rrd_vertical'})
  {
    push (@ret, '-v', $obj->{'rrd_vertical'});
  }

  if ($obj->{'custom_order'})
  {
    sort_idents_by_type_instance ($idents, $obj->{'custom_order'});
  }

  $obj->{'ds_names'} ||= {};
  my @names = map { $obj->{'ds_names'}{$_->{'type_instance'}} || $_->{'type_instance'} } (@$idents);

  for (my $i = 0; $i < @$idents; $i++)
  {
    my $ident = $idents->[$i];
    my $filename = ident_to_filename ($ident);

    if ($ds_name_len < length ($names[$i]))
    {
      $ds_name_len = length ($names[$i]);
    }
    
    # Escape colons _after_ the length has been checked.
    $names[$i] =~ s/:/\\:/g;

    push (@ret,
      "DEF:min${i}=${filename}:${data_source}:MIN",
      "DEF:avg${i}=${filename}:${data_source}:AVERAGE",
      "DEF:max${i}=${filename}:${data_source}:MAX");
  }

  for (my $i = 0; $i < @$idents; $i++)
  {
    my $type_instance = $idents->[$i]{'type_instance'};
    my $ds_name = sprintf ("%-*s", $ds_name_len, $names[$i]);
    my $color = '000000';
    if (exists $colors->{$type_instance})
    {
      $color = $colors->{$type_instance};
    }
    else
    {
      $color = get_random_color ();
    }
    push (@ret,
      "LINE1:avg${i}#${color}:${ds_name}",
      "GPRINT:min${i}:MIN:${format} Min,",
      "GPRINT:avg${i}:AVERAGE:${format} Avg,",
      "GPRINT:max${i}:MAX:${format} Max,",
      "GPRINT:avg${i}:LAST:${format} Last\\l");
  }

  return (\@ret);
}

sub getGraphArgs
{
  my $obj = shift;
  my $index = shift;

  my $group = _group_files ($obj);
  my $idents = $group->[$index];

  my @args = ();
  for (qw(hostname plugin plugin_instance type))
  {
    if (defined ($idents->[0]{$_}))
    {
      push (@args, $_ . '=' . $idents->[0]{$_});
    }
  }
  push (@args, "index=$index");

  return (join (';', @args));
} # getGraphArgs

# vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
