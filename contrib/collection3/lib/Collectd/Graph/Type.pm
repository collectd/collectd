package Collectd::Graph::Type;

=head1 NAME

Collectd::Graph::Type - Base class for the collectd graphing infrastructure

=cut

# Copyright (C) 2008  Florian octo Forster <octo at verplant.org>
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

use Carp (qw(confess cluck));
use RRDs ();
use URI::Escape (qw(uri_escape));

use Collectd::Graph::Common (qw($ColorCanvas $ColorFullBlue $ColorHalfBlue
  ident_to_filename
  ident_to_string
  get_faded_color));

return (1);

=head1 DESCRIPTION

This module serves as base class for more specialized classes realizing
specific "types".

=head1 MEMBER VARIABLES

As typical in Perl, a Collectd::Graph::Type object is a blessed hash reference.
Member variables are entries in that hash. Inheriting classes are free to add
additional entries. To set the member variable B<foo> to B<42>, do:

 $obj->{'foo'} = 42;

The following members control the behavior of Collectd::Graph::Type.

=over 4

=item B<files> (array reference)

List of RRD files. Each file is passed as "ident", i.E<nbsp>e. broken up into
"hostname", "plugin", "type" and optionally "plugin_instance" and
"type_instance". Use the B<addFiles> method rather than setting this directly.

=item B<data_sources> (array reference)

List of data sources in the RRD files. If this is not given, the default
implementation of B<getDataSources> will use B<RRDs::info> to find out which
data sources are contained in the files.

=item B<ds_names> (array reference)

Names of the data sources as printed in the graph. Should be in the same order
as the data sources are returned by B<getDataSources>.

=item B<rrd_title> (string)

Title of the RRD graph. The title can contain "{hostname}", "{plugin}" and so
on which are replaced with their actual value. See the B<getTitle> method
below.

=item B<rrd_opts> (array reference)

List of options directly passed to B<RRDs::graph>.

=item B<rrd_format> (string)

Format to use with B<GPRINT>. Defaults to C<%5.1lf>.

=item B<rrd_colors> (hash reference)

Mapping of data source names to colors, used when graphing the different data
sources.  Colors are given in the typical hexadecimal RGB form, but without
leading "#", e.E<nbsp>g.:

 $obj->{'rrd_colors'} = {foo => 'ff0000', bar => '00ff00'};

=back

=head1 METHODS

The following methods are used by the graphing front end and may be overwritten
to customize their behavior.

=over 4

=cut

sub _get_ds_from_file
{
  my $file = shift;
  my $info = RRDs::info ($file);
  my %ds = ();
  my @ds = ();

  if (!$info || (ref ($info) ne 'HASH'))
  {
    return;
  }

  for (keys %$info)
  {
    if (m/^ds\[([^\]]+)\]/)
    {
      $ds{$1} = 1;
    }
  }

  @ds = (keys %ds);
  if (wantarray ())
  {
    return (@ds);
  }
  elsif (@ds)
  {
    return (\@ds);
  }
  else
  {
    return;
  }
} # _get_ds_from_file

sub new
{
  my $pkg = shift;
  my $obj = bless ({files => []}, $pkg);

  if (@_)
  {
    $obj->addFiles (@_);
  }

  return ($obj);
}

=item B<addFiles> ({ I<ident> }, [...])

Adds the given idents (which are hash references) to the B<files> member
variable, see above.

=cut

sub addFiles
{
  my $obj = shift;
  push (@{$obj->{'files'}}, @_);
}

=item B<getGraphsNum> ()

Returns the number of graphs that can be generated from the added files. By
default this number equals the number of files.

=cut

sub getGraphsNum
{
  my $obj = shift;
  return (scalar @{$obj->{'files'}});
}

=item B<getDataSources> ()

Returns the names of the data sources. If the B<data_sources> member variable
is unset B<RRDs::info> is used to read that information from the first file.
Set the B<data_sources> member variable instead of overloading this method!

=cut

sub getDataSources
{
  my $obj = shift;

  if (!defined $obj->{'data_sources'})
  {
    my $ident;
    my $filename;

    if (!@{$obj->{'files'}})
    {
      return;
    }

    $ident = $obj->{'files'}[0];
    $filename = ident_to_filename ($ident);

    $obj->{'data_sources'} = _get_ds_from_file ($filename);
    if (!$obj->{'data_sources'})
    {
      cluck ("_get_ds_from_file ($filename) failed.");
    }
  }

  if (!defined $obj->{'data_sources'})
  {
    return;
  }
  elsif (wantarray ())
  {
    return (@{$obj->{'data_sources'}})
  }
  else
  {
    $obj->{'data_sources'};
  }
} # getDataSources


=item B<getTitle> (I<$index>)

Returns the title of the I<$index>th B<graph> (not necessarily file!). If the
B<rrd_title> member variable is unset, a generic title is generated from the
ident. Otherwise the substrings "{hostname}", "{plugin}", "{plugin_instance}",
"{type}", and "{type_instance}" are replaced by their respective values.

=cut

sub getTitle
{
  my $obj = shift;
  my $ident = shift;
  my $title = $obj->{'rrd_title'};

  if (!$title)
  {
    return (ident_to_string ($ident));
  }

  my $hostname = $ident->{'hostname'};
  my $plugin = $ident->{'plugin'};
  my $plugin_instance = $ident->{'plugin_instance'};
  my $type = $ident->{'type'};
  my $type_instance = $ident->{'type_instance'};
  my $instance;

  if ((defined $type_instance) && (defined $plugin_instance))
  {
    $instance = "$plugin_instance/$type_instance";
  }
  elsif (defined $type_instance)
  {
    $instance = $type_instance;
  }
  elsif (defined $plugin_instance)
  {
    $instance = $plugin_instance;
  }
  else
  {
    $instance = 'no instance';
  }

  if (!defined $plugin_instance)
  {
    $plugin_instance = 'no instance';
  }

  if (!defined $type_instance)
  {
    $type_instance = 'no instance';
  }

  $title =~ s#{hostname}#$hostname#g;
  $title =~ s#{plugin}#$plugin#g;
  $title =~ s#{plugin_instance}#$plugin_instance#g;
  $title =~ s#{type}#$type#g;
  $title =~ s#{type_instance}#$type_instance#g;
  $title =~ s#{instance}#$instance#g;

  return ($title);
}

=item B<getRRDArgs> (I<$index>)

Return the arguments needed to generate the graph from the RRD file(s). If the
file has only one data source, this default implementation will generate that
typical min, average, max graph you probably know from temperatures and such.
If the RRD files have multiple data sources, the average of each data source is
printes as simple line.

=cut

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

  my $rrd_opts = $obj->{'rrd_opts'} || [];
  my $rrd_title = $obj->getTitle ($ident);
  my $format = $obj->{'rrd_format'} || '%5.1lf';

  my $rrd_colors = $obj->{'rrd_colors'};
  my @ret = ('-t', $rrd_title, @$rrd_opts);

  if (defined $obj->{'rrd_vertical'})
  {
    push (@ret, '-v', $obj->{'rrd_vertical'});
  }

  my $ds_names = $obj->{'ds_names'};
  if (!$ds_names)
  {
    $ds_names = {};
  }

  my $ds = $obj->getDataSources ();
  if (!$ds)
  {
    confess ("obj->getDataSources failed.");
  }

  if (!$rrd_colors)
  {
    my @tmp = ('0000ff', 'ff0000', '00ff00', 'ff00ff', '00ffff', 'ffff00');

    for (my $i = 0; $i < @$ds; $i++)
    {
      $rrd_colors->{$ds->[$i]} = $tmp[$i % @tmp];
    }
  }

  for (my $i = 0; $i < @$ds; $i++)
  {
    my $f = $filename;
    my $ds_name = $ds->[$i];

    # We need to escape colons for RRDTool..
    $f =~ s#:#\\:#g;
    $ds_name =~ s#:#\\:#g;

    if (exists ($obj->{'scale'}))
    {
      my $scale = 0.0 + $obj->{'scale'};
      push (@ret,
	"DEF:min${i}_raw=${f}:${ds_name}:MIN",
	"DEF:avg${i}_raw=${f}:${ds_name}:AVERAGE",
	"DEF:max${i}_raw=${f}:${ds_name}:MAX",
	"CDEF:max${i}=max${i}_raw,$scale,*",
	"CDEF:avg${i}=avg${i}_raw,$scale,*",
	"CDEF:min${i}=min${i}_raw,$scale,*");
    }
    else
    {
      push (@ret,
	"DEF:min${i}=${f}:${ds_name}:MIN",
	"DEF:avg${i}=${f}:${ds_name}:AVERAGE",
	"DEF:max${i}=${f}:${ds_name}:MAX");
    }
  }

  if (@$ds == 1)
  {
    my $ds_name = $ds->[0];
    my $color_fg = $rrd_colors->{$ds_name} || '000000';
    my $color_bg = get_faded_color ($color_fg);

    if ($ds_names->{$ds_name})
    {
      $ds_name = $ds_names->{$ds_name};
    }
    $ds_name =~ s#:#\\:#g;

    push (@ret, 
      "AREA:max0#${color_bg}",
      "AREA:min0#${ColorCanvas}",
      "LINE1:avg0#${color_fg}:${ds_name}",
      "GPRINT:min0:MIN:${format} Min,",
      "GPRINT:avg0:AVERAGE:${format} Avg,",
      "GPRINT:max0:MAX:${format} Max,",
      "GPRINT:avg0:LAST:${format} Last\\l");
  }
  else
  {
    for (my $i = 0; $i < @$ds; $i++)
    {
      my $ds_name = $ds->[$i];
      my $color = $rrd_colors->{$ds_name} || '000000';

      if ($ds_names->{$ds_name})
      {
	$ds_name = $ds_names->{$ds_name};
      }

      push (@ret, 
	"LINE1:avg${i}#${color}:${ds_name}",
	"GPRINT:min${i}:MIN:${format} Min,",
	"GPRINT:avg${i}:AVERAGE:${format} Avg,",
	"GPRINT:max${i}:MAX:${format} Max,",
	"GPRINT:avg${i}:LAST:${format} Last\\l");
    }
  }

  return (\@ret);
} # getRRDArgs

=item B<getGraphArgs> (I<$index>)

Returns the parameters that should be passed to the CGI script to generate the
I<$index>th graph. The returned string is already URI-encoded and will possibly
set the "hostname", "plugin", "plugin_instance", "type", and "type_instance"
parameters.

The default implementation simply uses the ident of the I<$index>th file to
fill this.

=cut

sub getGraphArgs
{
  my $obj = shift;
  my $index = shift;
  my $ident = $obj->{'files'}[$index];

  my @args = ();
  for (qw(hostname plugin plugin_instance type type_instance))
  {
    if (defined ($ident->{$_}))
    {
      push (@args, uri_escape ($_) . '=' . uri_escape ($ident->{$_}));
    }
  }

  return (join (';', @args));
}

=item B<getLastModified> ([I<$index>])

If I<$index> is not given, the modification time of all files is scanned and the most recent modification is returned. If I<$index> is given, only the files belonging to the I<$index>th graph will be considered.

=cut

sub getLastModified
{
  my $obj = shift;
  my $index = @_ ? shift : -1;

  my $mtime = 0;

  if ($index == -1)
  {
    for (@{$obj->{'files'}})
    {
      my $ident = $_;
      my $filename = ident_to_filename ($ident);
      my @statbuf = stat ($filename);

      if (!@statbuf)
      {
	next;
      }

      if ($mtime < $statbuf[9])
      {
	$mtime = $statbuf[9];
      }
    }
  }
  else
  {
    my $ident = $obj->{'files'}[$index];
    my $filename = ident_to_filename ($ident);
    my @statbuf = stat ($filename);

    $mtime = $statbuf[9];
  }

  if (!$mtime)
  {
    return;
  }
  return ($mtime);
} # getLastModified

=back

=head1 SEE ALSO

L<Collectd::Graph::Type::GenericStacked>

=head1 AUTHOR AND LICENSE

Copyright (c) 2008 by Florian Forster
E<lt>octoE<nbsp>atE<nbsp>verplant.orgE<gt>. Licensed under the terms of the GNU
General Public License, VersionE<nbsp>2 (GPLv2).

=cut

# vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
