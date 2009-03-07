package Collectd::Graph::Config;

=head1 NAME

Collectd::Graph::Config - Parse the collection3 config file.

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

@Collectd::Graph::Config::ISA = ('Exporter');
@Collectd::Graph::Config::EXPORT_OK = (qw(gc_read_config gc_get_config
  gc_get_scalar));

our $Configuration = undef;

return (1);

=head1 EXPORTED FUNCTIONS

=over 4

=item B<gc_read_config> (I<$file>)

Reads the configuration from the file located at I<$file>. Returns B<true> when
successfull and B<false> otherwise.

=cut

sub gc_read_config
{
  my $file = shift;
  my %conf;

  if ($Configuration)
  {
    return (1);
  }

  $file ||= "etc/collection.conf";

  %conf = ParseConfig (-ConfigFile => $file,
    -LowerCaseNames => 1,
    -UseApacheInclude => 1,
    -IncludeDirectories => 1,
    ($Config::General::VERSION >= 2.38) ? (-IncludeAgain => 0) : (),
    -MergeDuplicateBlocks => 1,
    -CComments => 0);
  if (!%conf)
  {
    return;
  }

  $Configuration = \%conf;
  return (1);
} # gc_read_config

=item B<gc_get_config> ()

Returns the hash as provided by L<Config::General>. The hash is returned as a
hash reference. Don't change it!

=cut

sub gc_get_config
{
  return ($Configuration);
} # gc_get_config

=item B<gc_get_config> (I<$key>, [I<$default>])

Returns the scalar value I<$key> from the config file. If the key does not
exist, I<$default> will be returned. If no default is given, B<undef> will be
used in this case.

=cut

sub gc_get_scalar
{
  my $key = shift;
  my $default = (@_ != 0) ? shift : undef;
  my $value;

  if (!$Configuration)
  {
    return ($default);
  }

  $value = $Configuration->{lc ($key)};
  if (!defined ($value))
  {
    return ($default);
  }

  if (ref ($value) ne '')
  {
    cluck ("Value for `$key' should be scalar, but actually is "
      . ref ($value));
    return ($default);
  }

  return ($value);
} # gc_get_config

=back

=head1 DEPENDS ON

L<Config::General>

=head1 SEE ALSO

L<Collectd::Graph::Type>

=head1 AUTHOR AND LICENSE

Copyright (c) 2008 by Florian Forster
E<lt>octoE<nbsp>atE<nbsp>verplant.orgE<gt>. Licensed under the terms of the GNU
General Public License, VersionE<nbsp>2 (GPLv2).

=cut

# vim: set shiftwidth=2 softtabstop=2 tabstop=8 et fdm=marker :
