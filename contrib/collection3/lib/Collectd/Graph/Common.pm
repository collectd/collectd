package Collectd::Graph::Common;

# Copyright (C) 2008-2011  Florian Forster
# Copyright (C) 2011       noris network AG
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
#
# Authors:
#   Florian "octo" Forster <octo at collectd.org>

use strict;
use warnings;

use vars (qw($ColorCanvas $ColorFullBlue $ColorHalfBlue));

use Collectd::Unixsock ();
use Carp (qw(confess cluck));
use CGI (':cgi');
use Exporter;
use Collectd::Graph::Config (qw(gc_get_scalar));

our $Cache = {};

$ColorCanvas   = 'FFFFFF';
$ColorFullBlue = '0000FF';
$ColorHalfBlue = 'B7B7F7';

@Collectd::Graph::Common::ISA = ('Exporter');
@Collectd::Graph::Common::EXPORT_OK = (qw(
  $ColorCanvas
  $ColorFullBlue
  $ColorHalfBlue

  sanitize_hostname
  sanitize_plugin sanitize_plugin_instance
  sanitize_type sanitize_type_instance
  group_files_by_plugin_instance
  get_files_from_directory
  filename_to_ident
  ident_to_filename
  ident_to_string
  get_all_hosts
  get_files_for_host
  get_files_by_ident
  get_selected_files
  get_timespan_selection
  get_host_selection
  get_plugin_selection
  get_random_color
  get_faded_color
  sort_idents_by_type_instance
  type_to_module_name
  epoch_to_rfc1123
  flush_files
));

our $DefaultDataDir = '/var/lib/collectd/rrd';

return (1);

sub _sanitize_generic_allow_minus
{
  my $str = "" . shift;

  # remove all slashes
  $str =~ s#/##g;

  # remove all dots and dashes at the beginning and at the end.
  $str =~ s#^[\.-]+##;
  $str =~ s#[\.-]+$##;

  return ($str);
}

sub _sanitize_generic_no_minus
{
  # Do everything the allow-minus variant does..
  my $str = _sanitize_generic_allow_minus (@_);

  # .. and remove the dashes, too
  $str =~ s#/##g;

  return ($str);
} # _sanitize_generic_no_minus

sub sanitize_hostname
{
  return (_sanitize_generic_allow_minus (@_));
}

sub sanitize_plugin
{
  return (_sanitize_generic_no_minus (@_));
}

sub sanitize_plugin_instance
{
  return (_sanitize_generic_allow_minus (@_));
}

sub sanitize_type
{
  return (_sanitize_generic_no_minus (@_));
}

sub sanitize_type_instance
{
  return (_sanitize_generic_allow_minus (@_));
}

sub group_files_by_plugin_instance
{
  my @files = @_;
  my $data = {};

  for (my $i = 0; $i < @files; $i++)
  {
    my $file = $files[$i];
    my $key1 = $file->{'hostname'} || '';
    my $key2 = $file->{'plugin_instance'} || '';
    my $key = "$key1-$key2";

    $data->{$key} ||= [];
    push (@{$data->{$key}}, $file);
  }

  return ($data);
}

sub filename_to_ident
{
  my $file = shift;
  my $ret;

  if ($file =~ m#([^/]+)/([^/\-]+)(?:-([^/]+))?/([^/\-]+)(?:-([^/]+))?\.rrd$#)
  {
    $ret = {hostname => $1, plugin => $2, type => $4};
    if (defined ($3))
    {
      $ret->{'plugin_instance'} = $3;
    }
    if (defined ($5))
    {
      $ret->{'type_instance'} = $5;
    }
    if ($`)
    {
      $ret->{'_prefix'} = $`;
    }
  }
  else
  {
    return;
  }

  return ($ret);
} # filename_to_ident

sub ident_to_filename
{
  my $ident = shift;
  my $data_dir = gc_get_scalar ('DataDir', $DefaultDataDir);

  my $ret = '';

  if (defined ($ident->{'_prefix'}))
  {
    $ret .= $ident->{'_prefix'};
  }
  else
  {
    $ret .= "$data_dir/";
  }

  if (!$ident->{'hostname'})
  {
    cluck ("hostname is undefined")
  }
  if (!$ident->{'plugin'})
  {
    cluck ("plugin is undefined")
  }
  if (!$ident->{'type'})
  {
    cluck ("type is undefined")
  }

  $ret .= $ident->{'hostname'} . '/' . $ident->{'plugin'};
  if (defined ($ident->{'plugin_instance'}))
  {
    $ret .= '-' . $ident->{'plugin_instance'};
  }

  $ret .= '/' . $ident->{'type'};
  if (defined ($ident->{'type_instance'}))
  {
    $ret .= '-' . $ident->{'type_instance'};
  }
  $ret .= '.rrd';

  return ($ret);
} # ident_to_filename

sub _part_to_string
{
  my $part = shift;

  if (!defined ($part))
  {
    return ("(UNDEF)");
  }
  if (ref ($part) eq 'ARRAY')
  {
    if (1 == @$part)
    {
      return ($part->[0]);
    }
    else
    {
      return ('(' . join (',', @$part) . ')');
    }
  }
  else
  {
    return ($part);
  }
} # _part_to_string

sub ident_to_string
{
  my $ident = shift;

  my $ret = '';

  $ret .= _part_to_string ($ident->{'hostname'})
  . '/' . _part_to_string ($ident->{'plugin'});
  if (defined ($ident->{'plugin_instance'}))
  {
    $ret .= '-' . _part_to_string ($ident->{'plugin_instance'});
  }

  $ret .= '/' . _part_to_string ($ident->{'type'});
  if (defined ($ident->{'type_instance'}))
  {
    $ret .= '-' . _part_to_string ($ident->{'type_instance'});
  }

  return ($ret);
} # ident_to_string

sub get_files_from_directory
{
  my $dir = shift;
  my $recursive = @_ ? shift : 0;
  my $dh;
  my @directories = ();
  my @files = ();
  my $ret = [];

  opendir ($dh, $dir) or die ("opendir ($dir): $!");
  while (my $entry = readdir ($dh))
  {
    next if ($entry =~ m/^\./);

    $entry = "$dir/$entry";

    if (-d $entry)
    {
      push (@directories, $entry);
    }
    elsif (-f $entry)
    {
      push (@files, $entry);
    }
  }
  closedir ($dh);

  push (@$ret, map { filename_to_ident ($_) } sort (@files));

  if ($recursive > 0)
  {
    for (@directories)
    {
      my $temp = get_files_from_directory ($_, $recursive - 1);
      if ($temp && @$temp)
      {
        push (@$ret, @$temp);
      }
    }
  }

  return ($ret);
} # get_files_from_directory

sub get_all_hosts
{
  my $ret = [];

  if (defined ($Cache->{'get_all_hosts'}))
  {
    $ret = $Cache->{'get_all_hosts'};
  }
  else
  {
    my $dh;
    my $data_dir = gc_get_scalar ('DataDir', $DefaultDataDir);

    opendir ($dh, "$data_dir") or confess ("opendir ($data_dir): $!");
    while (my $entry = readdir ($dh))
    {
      next if ($entry =~ m/^\./);
      next if (!-d "$data_dir/$entry");
      push (@$ret, sanitize_hostname ($entry));
    }
    closedir ($dh);

    $Cache->{'get_all_hosts'} = $ret;
  }

  if (wantarray ())
  {
    return (@$ret);
  }
  elsif (@$ret)
  {
    return ($ret);
  }
  else
  {
    return;
  }
} # get_all_hosts

sub get_all_plugins
{
  my @hosts = @_;
  my $ret = {};
  my $dh;
  my $data_dir = gc_get_scalar ('DataDir', $DefaultDataDir);
  my $cache_key;

  if (@hosts)
  {
    $cache_key = join (';', @hosts);
  }
  else
  {
    $cache_key = "/*/";
    @hosts = get_all_hosts ();
  }

  if (defined ($Cache->{'get_all_plugins'}{$cache_key}))
  {
    $ret = $Cache->{'get_all_plugins'}{$cache_key};

    if (wantarray ())
    {
      return (sort (keys %$ret));
    }
    else
    {
      return ($ret);
    }
  }

  for (@hosts)
  {
    my $host = $_;
    opendir ($dh, "$data_dir/$host") or next;
    while (my $entry = readdir ($dh))
    {
      my $plugin;
      my $plugin_instance = '';

      next if ($entry =~ m/^\./);
      next if (!-d "$data_dir/$host/$entry");

      if ($entry =~ m#^([^-]+)-(.+)$#)
      {
	$plugin = $1;
	$plugin_instance = $2;
      }
      elsif ($entry =~ m#^([^-]+)$#)
      {
	$plugin = $1;
	$plugin_instance = '';
      }
      else
      {
	next;
      }

      $ret->{$plugin} ||= {};
      $ret->{$plugin}{$plugin_instance} = 1;
    } # while (readdir)
    closedir ($dh);
  } # for (@hosts)

  $Cache->{'get_all_plugins'}{$cache_key} = $ret;
  if (wantarray ())
  {
    return (sort (keys %$ret));
  }
  else
  {
    return ($ret);
  }
} # get_all_plugins

sub get_files_for_host
{
  my $host = sanitize_hostname (shift);
  my $data_dir = gc_get_scalar ('DataDir', $DefaultDataDir);
  return (get_files_from_directory ("$data_dir/$host", 2));
} # get_files_for_host

sub _filter_ident
{
  my $filter = shift;
  my $ident = shift;

  for (qw(hostname plugin plugin_instance type type_instance))
  {
    my $part = $_;
    my $tmp;

    if (!defined ($filter->{$part}))
    {
      next;
    }
    if (!defined ($ident->{$part}))
    {
      return (1);
    }

    if (ref $filter->{$part})
    {
      if (!grep { $ident->{$part} eq $_ } (@{$filter->{$part}}))
      {
	return (1);
      }
    }
    else
    {
      if ($ident->{$part} ne $filter->{$part})
      {
	return (1);
      }
    }
  }

  return (0);
} # _filter_ident

sub _get_all_files
{
  my $ret;

  if (defined ($Cache->{'_get_all_files'}))
  {
    $ret = $Cache->{'_get_all_files'};
  }
  else
  {
    my $data_dir = gc_get_scalar ('DataDir', $DefaultDataDir);

    $ret = get_files_from_directory ($data_dir, 3);
    $Cache->{'_get_all_files'} = $ret;
  }

  return ($ret);
} # _get_all_files

sub get_files_by_ident
{
  my $ident = shift;
  my $all_files;
  my @ret = ();

  my $cache_key = ident_to_string ($ident);
  if (defined ($Cache->{'get_files_by_ident'}{$cache_key}))
  {
    my $ret = $Cache->{'get_files_by_ident'}{$cache_key};

    return ($ret)
  }

  $all_files = _get_all_files ();

  @ret = grep { _filter_ident ($ident, $_) == 0 } (@$all_files);

  $Cache->{'get_files_by_ident'}{$cache_key} = \@ret;
  return (\@ret);
} # get_files_by_ident

sub get_selected_files
{
  my $ident = {};
  
  for (qw(hostname plugin plugin_instance type type_instance))
  {
    my $part = $_;
    my @temp = param ($part);
    if (!@temp)
    {
      next;
    }
    elsif (($part eq 'plugin') || ($part eq 'type'))
    {
      $ident->{$part} = [map { _sanitize_generic_no_minus ($_) } (@temp)];
    }
    else
    {
      $ident->{$part} = [map { _sanitize_generic_allow_minus ($_) } (@temp)];
    }
  }

  return (get_files_by_ident ($ident));
} # get_selected_files

sub get_timespan_selection
{
  my $ret = 86400;
  if (param ('timespan'))
  {
    my $temp = int (param ('timespan'));
    if ($temp && ($temp > 0))
    {
      $ret = $temp;
    }
  }

  return ($ret);
} # get_timespan_selection

sub get_host_selection
{
  my %ret = ();

  for (get_all_hosts ())
  {
    $ret{$_} = 0;
  }

  for (param ('hostname'))
  {
    my $host = _sanitize_generic_allow_minus ($_);
    if (defined ($ret{$host}))
    {
      $ret{$host} = 1;
    }
  }

  if (wantarray ())
  {
    return (grep { $ret{$_} > 0 } (sort (keys %ret)));
  }
  else
  {
    return (\%ret);
  }
} # get_host_selection

sub get_plugin_selection
{
  my %ret = ();
  my @hosts = get_host_selection ();

  for (get_all_plugins (@hosts))
  {
    $ret{$_} = 0;
  }

  for (param ('plugin'))
  {
    if (defined ($ret{$_}))
    {
      $ret{$_} = 1;
    }
  }

  if (wantarray ())
  {
    return (grep { $ret{$_} > 0 } (sort (keys %ret)));
  }
  else
  {
    return (\%ret);
  }
} # get_plugin_selection

sub _string_to_color
{
  my $color = shift;
  if ($color =~ m/([0-9A-Fa-f][0-9A-Fa-f])([0-9A-Fa-f][0-9A-Fa-f])([0-9A-Fa-f][0-9A-Fa-f])/)
  {
    return ([hex ($1) / 255.0, hex ($2) / 255.0, hex ($3) / 255.0]);
  }
  return;
} # _string_to_color

sub _color_to_string
{
  confess ("Wrong number of arguments") if (@_ != 1);
  return (sprintf ('%02hx%02hx%02hx', map { int (255.0 * $_) } @{$_[0]}));
} # _color_to_string

sub get_random_color
{
  my ($r, $g, $b) = (rand (), rand ());
  my $min = 0.0;
  my $max = 1.0;

  if (($r + $g) < 1.0)
  {
    $min = 1.0 - ($r + $g);
  }
  else
  {
    $max = 2.0 - ($r + $g);
  }

  $b = $min + (rand () * ($max - $min));

  return (_color_to_string ([$r, $g, $b]));
} # get_random_color

sub get_faded_color
{
  my $fg = shift;
  my $bg;
  my %opts = @_;
  my $ret = [undef, undef, undef];

  $opts{'background'} ||= [1.0, 1.0, 1.0];
  $opts{'alpha'} ||= 0.25;

  if (!ref ($fg))
  {
    $fg = _string_to_color ($fg)
      or confess ("Cannot parse foreground color $fg");
  }

  if (!ref ($opts{'background'}))
  {
    $opts{'background'} = _string_to_color ($opts{'background'})
      or confess ("Cannot parse background color " . $opts{'background'});
  }
  $bg = $opts{'background'};

  for (my $i = 0; $i < 3; $i++)
  {
    $ret->[$i] = ($opts{'alpha'} * $fg->[$i])
       + ((1.0 - $opts{'alpha'}) * $bg->[$i]);
  }

  return (_color_to_string ($ret));
} # get_faded_color

sub sort_idents_by_type_instance
{
  my $idents = shift;
  my $array_sort = shift;

  my %elements = map { $_->{'type_instance'} => $_ } (@$idents);
  splice (@$idents, 0);

  for (@$array_sort)
  {
    next if (!exists ($elements{$_}));
    push (@$idents, $elements{$_});
    delete ($elements{$_});
  }
  push (@$idents, map { $elements{$_} } (sort (keys %elements)));
} # sort_idents_by_type_instance

sub type_to_module_name
{
  my $type = shift;
  my $ret;
  
  $ret = ucfirst (lc ($type));

  $ret =~ s/[^A-Za-z_]//g;
  $ret =~ s/_([A-Za-z])/\U$1\E/g;

  return ("Collectd::Graph::Type::$ret");
} # type_to_module_name

sub epoch_to_rfc1123
{
  my @days = (qw(Sun Mon Tue Wed Thu Fri Sat));
  my @months = (qw(Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec));

  my $epoch = @_ ? shift : time ();
  my ($sec, $min, $hour, $mday, $mon, $year, $wday, $yday) = gmtime($epoch);
  my $string = sprintf ('%s, %02d %s %4d %02d:%02d:%02d GMT', $days[$wday], $mday,
      $months[$mon], 1900 + $year, $hour ,$min, $sec);
  return ($string);
}

sub flush_files
{
  my $all_files = shift;
  my %opts = @_;

  my $begin;
  my $end;
  my $addr;
  my $interval;
  my $sock;
  my $now;
  my $files_to_flush = [];
  my $status;

  if (!defined $opts{'begin'})
  {
    cluck ("begin is not defined");
    return;
  }
  $begin = $opts{'begin'};

  if (!defined $opts{'end'})
  {
    cluck ("end is not defined");
    return;
  }
  $end = $opts{'end'};

  if (!$opts{'addr'})
  {
    return (1);
  }

  $interval = $opts{'interval'} || 10;

  if (ref ($all_files) eq 'HASH')
  {
    my @tmp = ($all_files);
    $all_files = \@tmp;
  }

  $now = time ();
  # Don't flush anything if the timespan is in the future.
  if (($end > $now) && ($begin > $now))
  {
    return (1);
  }

  for (@$all_files)
  {
    my $file_orig = $_;
    my $file_name = ident_to_filename ($file_orig);
    my $file_copy = {};
    my @statbuf;
    my $mtime;

    @statbuf = stat ($file_name);
    if (!@statbuf)
    {
      next;
    }
    $mtime = $statbuf[9];

    # Skip if file is fresh
    if (($now - $mtime) <= $interval)
    {
      next;
    }
    # or $end is before $mtime
    elsif (($end != 0) && (($end - $mtime) <= 0))
    {
      next;
    }

    $file_copy->{'host'} = $file_orig->{'hostname'};
    $file_copy->{'plugin'} = $file_orig->{'plugin'};
    if (exists $file_orig->{'plugin_instance'})
    {
      $file_copy->{'plugin_instance'} = $file_orig->{'plugin_instance'}
    }
    $file_copy->{'type'} = $file_orig->{'type'};
    if (exists $file_orig->{'type_instance'})
    {
      $file_copy->{'type_instance'} = $file_orig->{'type_instance'}
    }

    push (@$files_to_flush, $file_copy);
  } # for (@$all_files)

  if (!@$files_to_flush)
  {
    return (1);
  }

  $sock = Collectd::Unixsock->new ($opts{'addr'});
  if (!$sock)
  {
    return;
  }

  $status = $sock->flush (plugins => ['rrdtool'], identifier => $files_to_flush);
  if (!$status)
  {
    cluck ("FLUSH failed: " . $sock->{'error'});
    $sock->destroy ();
    return;
  }

  $sock->destroy ();
  return (1);
} # flush_files

# vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
