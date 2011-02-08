#!/usr/bin/perl

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
use utf8;
use vars (qw($BASE_DIR));

BEGIN
{
  if (defined $ENV{'SCRIPT_FILENAME'})
  {
    if ($ENV{'SCRIPT_FILENAME'} =~ m{^(/.+)/bin/[^/]+$})
    {
      $::BASE_DIR = $1;
      unshift (@::INC, "$::BASE_DIR/lib");
    }
  }
}

use Carp (qw(confess cluck));
use CGI (':cgi');
use RRDs ();
use File::Temp (':POSIX');

use Collectd::Graph::Config (qw(gc_read_config gc_get_scalar));
use Collectd::Graph::TypeLoader (qw(tl_load_type));

use Collectd::Graph::Common (qw(sanitize_type get_selected_files
      epoch_to_rfc1123 flush_files));
use Collectd::Graph::Type ();

sub base_dir
{
  if (defined $::BASE_DIR)
  {
    return ($::BASE_DIR);
  }

  if (!defined ($ENV{'SCRIPT_FILENAME'}))
  {
    return;
  }

  if ($ENV{'SCRIPT_FILENAME'} =~ m{^(/.+)/bin/[^/]+$})
  {
    $::BASE_DIR = $1;
    return ($::BASE_DIR);
  }

  return;
}

sub lib_dir
{
  my $base = base_dir ();

  if ($base)
  {
    return "$base/lib";
  }
  else
  {
    return "../lib";
  }
}

sub sysconf_dir
{
  my $base = base_dir ();

  if ($base)
  {
    return "$base/etc";
  }
  else
  {
    return "../etc";
  }
}

sub init
{
  my $lib_dir = lib_dir ();
  my $sysconf_dir = sysconf_dir ();

  if (!grep { $lib_dir eq $_ } (@::INC))
  {
    unshift (@::INC, $lib_dir);
  }

  gc_read_config ("$sysconf_dir/collection.conf");
}

sub main
{
  my $Begin = param ('begin');
  my $End = param ('end');
  my $GraphWidth = param ('width');
  my $GraphHeight = param ('height');
  my $Index = param ('index') || 0;
  my $OutputFormat = 'PNG';
  my $ContentType = 'image/png';

  init ();

  if (param ('format'))
  {
    my $temp = param ('format') || '';
    $temp = uc ($temp);

    if ($temp =~ m/^(PNG|SVG|EPS|PDF)$/)
    {
      $OutputFormat = $temp;

      if ($OutputFormat eq 'SVG') { $ContentType = 'image/svg+xml'; }
      elsif ($OutputFormat eq 'EPS') { $ContentType = 'image/eps'; }
      elsif ($OutputFormat eq 'PDF') { $ContentType = 'application/pdf'; }
    }
  }

  if (param ('debug'))
  {
    print <<HTTP;
Content-Type: text/plain

HTTP
    $ContentType = 'text/plain';
  }

  if ($GraphWidth)
  {
    $GraphWidth =~ s/\D//g;
  }

  if (!$GraphWidth)
  {
    $GraphWidth = gc_get_scalar ('GraphWidth', 400);
  }

  if ($GraphHeight)
  {
    $GraphHeight =~ s/\D//g;
  }

  if (!$GraphHeight)
  {
    $GraphHeight = gc_get_scalar ('GraphHeight', 100);
  }

  { # Sanitize begin and end times
    $End ||= 0;
    $Begin ||= 0;

    if ($End =~ m/\D/)
    {
      $End = 0;
    }

    if (!$Begin || !($Begin =~ m/^-?([1-9][0-9]*)$/))
    {
      $Begin = -86400;
    }

    if ($Begin < 0)
    {
      if ($End)
      {
        $Begin = $End + $Begin;
      }
      else
      {
        $Begin = time () + $Begin;
      }
    }

    if ($Begin < 0)
    {
      $Begin = time () - 86400;
    }

    if (($End > 0) && ($Begin > $End))
    {
      my $temp = $End;
      $End = $Begin;
      $Begin = $temp;
    }
  }

  my $type = param ('type') or die;
  my $obj;

  $obj = tl_load_type ($type);
  if (!$obj)
  {
    confess ("tl_load_type ($type) failed");
  }

  $type = ucfirst (lc ($type));
  $type =~ s/_([A-Za-z])/\U$1\E/g;
  $type = sanitize_type ($type);

  my $files = get_selected_files ();
  if (param ('debug'))
  {
    require Data::Dumper;
    print Data::Dumper->Dump ([$files], ['files']);
  }
  for (@$files)
  {
    $obj->addFiles ($_);
  }

  my $expires = time ();
# IF (End is `now')
#    OR (Begin is before `now' AND End is after `now')
  if (($End == 0) || (($Begin <= $expires) && ($End >= $expires)))
  {
    # 400 == width in pixels
    my $timespan;

    if ($End == 0)
    {
      $timespan = $expires - $Begin;
    }
    else
    {
      $timespan = $End - $Begin;
    }
    $expires += int ($timespan / 400.0);
  }
# IF (End is not `now')
#    AND (End is before `now')
# ==> Graph will never change again!
  elsif (($End > 0) && ($End < $expires))
  {
    $expires += (366 * 86400);
  }
  elsif ($Begin > $expires)
  {
    $expires = $Begin;
  }

# Send FLUSH command to the daemon if necessary and possible.
  flush_files ($files,
    begin => $Begin,
    end => $End,
    addr => gc_get_scalar ('UnixSockAddr', undef),
    interval => gc_get_scalar ('Interval', 10));

  print header (-Content_type => $ContentType,
    -Last_Modified => epoch_to_rfc1123 ($obj->getLastModified ()),
    -Expires => epoch_to_rfc1123 ($expires));

  if (param ('debug'))
  {
    print "\$expires = $expires;\n";
  }

  my $args = $obj->getRRDArgs (0 + $Index);
  if (param ('debug'))
  {
    require Data::Dumper;
    print Data::Dumper->Dump ([$obj], ['obj']);
    print join (",\n", @$args) . "\n";
    print "Last-Modified: " . epoch_to_rfc1123 ($obj->getLastModified ()) . "\n";
  }
  else
  {
    my @timesel = ();
    my $tmpfile = tmpnam ();
    my $status;

    if ($End) # $Begin is always true
    {
      @timesel = ('-s', $Begin, '-e', $End);
    }
    else
    {
      @timesel = ('-s', $Begin); # End is implicitely `now'.
    }

    if (-S "/var/run/rrdcached.sock" && -w "/var/run/rrdcached.sock")
    {
      $ENV{"RRDCACHED_ADDRESS"} = "/var/run/rrdcached.sock";
    }
    unlink ($tmpfile);
    RRDs::graph ($tmpfile, '-a', $OutputFormat, '--width', $GraphWidth, '--height', $GraphHeight, @timesel, @$args);
    if (my $err = RRDs::error ())
    {
      print STDERR "RRDs::graph failed: $err\n";
      exit (1);
    }

    $status = open (IMG, '<', $tmpfile) or die ("open ($tmpfile): $!");
    if (!$status)
    {
      print STDERR "graph.cgi: Unable to open temporary file \"$tmpfile\" for reading: $!\n";
    }
    else
    {
      local $/ = undef;
      while (my $data = <IMG>)
      {
        print STDOUT $data;
      }

      close (IMG);
      unlink ($tmpfile);
    }
  }
} # sub main

main ();

# vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
