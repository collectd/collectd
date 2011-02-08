package Collectd::Graph::TypeLoader;

=head1 NAME

Collectd::Graph::TypeLoader - Load a module according to the "type"

=cut

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

use Carp (qw(cluck confess));
use Exporter ();
use Config::General ('ParseConfig');
use Collectd::Graph::Config ('gc_get_config');
use Collectd::Graph::Type ();

@Collectd::Graph::TypeLoader::ISA = ('Exporter');
@Collectd::Graph::TypeLoader::EXPORT_OK = ('tl_load_type');

our @ArrayMembers = (qw(data_sources rrd_opts custom_order));
our @ScalarMembers = (qw(rrd_title rrd_format rrd_vertical scale ignore_unknown stacking));
our @DSMappedMembers = (qw(ds_names rrd_colors));

our %MemberToConfigMap =
(
  data_sources => 'datasources',
  ds_names => 'dsname',
  rrd_title => 'rrdtitle',
  rrd_opts => 'rrdoptions',
  rrd_format => 'rrdformat',
  rrd_vertical => 'rrdverticallabel',
  rrd_colors => 'color',
  scale => 'scale', # GenericIO only
  custom_order => 'order', # GenericStacked only
  stacking => 'stacking', # GenericStacked only
  ignore_unknown => 'ignoreunknown' # GenericStacked only
);

return (1);

sub _create_object
{
  my $module = shift;
  my $obj;

  # Surpress warnings and error messages caused by the eval.
  local $SIG{__WARN__} = sub { return (1); print STDERR "WARNING: " . join (', ', @_) . "\n"; };
  local $SIG{__DIE__}  = sub { return (1); print STDERR "FATAL: "   . join (', ', @_) . "\n"; };

  eval <<PERL;
  require $module;
  \$obj = ${module}->new ();
PERL
  if (!$obj)
  {
    return;
  }

  return ($obj);
} # _create_object

sub _load_module_from_config
{
  my $conf = shift;

  my $module = $conf->{'module'};
  my $obj;
  
  if ($module && !($module =~ m/::/))
  {
    $module = "Collectd::Graph::Type::$module";
  }

  if ($module)
  {
    $obj = _create_object ($module);
    if (!$obj)
    {
      #cluck ("Creating an $module object failed");
      warn ("Creating an $module object failed");
      return;
    }
  }
  else
  {
    $obj = Collectd::Graph::Type->new ();
    if (!$obj)
    {
      cluck ("Creating an Collectd::Graph::Type object failed");
      return;
    }
  }

  for (@ScalarMembers) # {{{
  {
    my $member = $_;
    my $key = $MemberToConfigMap{$member};
    my $val;

    if (!defined $conf->{$key})
    {
      next;
    }
    $val = $conf->{$key};
    
    if (ref ($val) ne '')
    {
      cluck ("Invalid value type for $key: " . ref ($val));
      next;
    }

    $obj->{$member} = $val;
  } # }}}

  for (@ArrayMembers) # {{{
  {
    my $member = $_;
    my $key = $MemberToConfigMap{$member};
    my $val;

    if (!defined $conf->{$key})
    {
      next;
    }
    $val = $conf->{$key};
    
    if (ref ($val) eq 'ARRAY')
    {
      $obj->{$member} = $val;
    }
    elsif (ref ($val) eq '')
    {
      $obj->{$member} = [split (' ', $val)];
    }
    else
    {
      cluck ("Invalid value type for $key: " . ref ($val));
    }
  } # }}}

  for (@DSMappedMembers) # {{{
  {
    my $member = $_;
    my $key = $MemberToConfigMap{$member};
    my @val_list;

    if (!defined $conf->{$key})
    {
      next;
    }

    if (ref ($conf->{$key}) eq 'ARRAY')
    {
      @val_list = @{$conf->{$key}};
    }
    elsif (ref ($conf->{$key}) eq '')
    {
      @val_list = ($conf->{$key});
    }
    else
    {
      cluck ("Invalid value type for $key: " . ref ($conf->{$key}));
      next;
    }

    for (@val_list)
    {
      my $line = $_;
      my $ds;
      my $val;

      if (!defined ($line) || (ref ($line) ne ''))
      {
        next;
      }

      ($ds, $val) = split (' ', $line, 2);
      if (!$ds || !$val)
      {
        next;
      }

      $obj->{$member} ||= {};
      $obj->{$member}{$ds} = $val;
    } # for (@val_list)
  } # }}} for (@DSMappedMembers)

  return ($obj);
} # _load_module_from_config

sub _load_module_generic
{
  my $type = shift;
  my $module = ucfirst (lc ($type));
  my $obj;

  $module =~ s/[^A-Za-z_]//g;
  $module =~ s/_([A-Za-z])/\U$1\E/g;

  $obj = _create_object ($module);
  if (!$obj)
  {
    $obj = Collectd::Graph::Type->new ();
    if (!$obj)
    {
      cluck ("Creating an Collectd::Graph::Type object failed");
      return;
    }
  }

  return ($obj);
} # _load_module_generic

=head1 EXPORTED FUNCTIONS

=over 4

=item B<tl_load_type> (I<$type>)

Does whatever is necessary to get an object with which to graph RRD files of
type I<$type>.

=cut

sub tl_load_type
{
  my $type = shift;
  my $conf = gc_get_config ();

  if (defined ($conf) && defined ($conf->{'type'}{$type}))
  {
    return (_load_module_from_config ($conf->{'type'}{$type}));
  }
  else
  {
    return (_load_module_generic ($type));
  }
} # tl_load_type

=back

=head1 SEE ALSO

L<Collectd::Graph::Type::GenericStacked>

=head1 AUTHOR AND LICENSE

Copyright (c) 2008 by Florian Forster
E<lt>octoE<nbsp>atE<nbsp>verplant.orgE<gt>. Licensed under the terms of the GNU
General Public License, VersionE<nbsp>2 (GPLv2).

=cut

# vim: set shiftwidth=2 softtabstop=2 tabstop=8 et fdm=marker :
