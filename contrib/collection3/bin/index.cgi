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

use Carp (qw(cluck confess));
use CGI (':cgi');
use CGI::Carp ('fatalsToBrowser');
use HTML::Entities ('encode_entities');

use Data::Dumper;

use Collectd::Graph::Config (qw(gc_read_config gc_get_scalar));
use Collectd::Graph::TypeLoader (qw(tl_load_type));
use Collectd::Graph::Common (qw(get_files_from_directory get_all_hosts
      get_timespan_selection get_selected_files get_host_selection
      get_plugin_selection flush_files));
use Collectd::Graph::Type ();

our $TimeSpans =
{
  Hour  =>        3600,
  Day   =>       86400,
  Week  =>   7 * 86400,
  Month =>  31 * 86400,
  Year  => 366 * 86400
};

my %Actions =
(
  list_hosts => \&action_list_hosts,
  show_selection => \&action_show_selection
);

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
  my $Debug = param ('debug') ? 1 : 0;
  my $action = param ('action') || 'list_hosts';

  if (!exists ($Actions{$action}))
  {
    print STDERR "No such action: $action\n";
    return (1);
  }

  init ();

  $Actions{$action}->();
  return (1);
} # sub main

sub can_handle_xhtml
{
  my %types = ();

  if (!defined $ENV{'HTTP_ACCEPT'})
  {
    return;
  }

  for (split (',', $ENV{'HTTP_ACCEPT'}))
  {
    my $type = lc ($_);
    my $q = 1.0;

    if ($type =~ m#^([^;]+);q=([0-9\.]+)$#)
    {
      $type = $1;
      $q = 0.0 + $2;
    }
    $types{$type} = $q;
  }

  if (!defined ($types{'application/xhtml+xml'}))
  {
    return;
  }
  elsif (!defined ($types{'text/html'}))
  {
    return (1);
  }
  elsif ($types{'application/xhtml+xml'} < $types{'text/html'})
  {
    return;
  }
  else
  {
    return (1);
  }
} # can_handle_xhtml

my $html_started;
sub start_html
{
  return if ($html_started);

  my $end;
  my $begin;
  my $timespan;

  $end = time ();
  $timespan = get_timespan_selection ();
  $begin = $end - $timespan;

  if (can_handle_xhtml ())
  {
    print header (-Content_Type => 'application/xhtml+xml; charset=UTF-8');
    print <<HTML;
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN"
    "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml"
    xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
    xsi:schemaLocation="http://www.w3.org/MarkUp/SCHEMA/xhtml11.xsd"
    xml:lang="en">
HTML
  }
  else
  {
    print header (-Content_Type => 'text/html; charset=UTF-8');
    print <<HTML;
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01//EN"
    "http://www.w3.org/TR/html4/strict.dtd">
<html>
HTML
  }
  print <<HTML;
  <head>
    <title>collection.cgi, Version 3</title>
    <link rel="icon" href="../share/shortcut-icon.png" type="image/png" />
    <link rel="stylesheet" href="../share/style.css" type="text/css" />
    <script type="text/javascript" src="../share/navigate.js"></script>
  </head>
  <body onload="nav_init ($begin, $end);">
HTML
  $html_started = 1;
}

sub end_html
{
  print <<HTML;
  </body>
</html>
HTML
  $html_started = 0;
}

sub show_selector
{
  my $timespan_selection = get_timespan_selection ();
  my $host_selection = get_host_selection ();
  my $plugin_selection = get_plugin_selection ();

  print <<HTML;
    <form action="${\script_name ()}" method="get">
      <fieldset>
        <legend>Data selection</legend>
        <select name="hostname" multiple="multiple" size="15">
HTML
  for (sort (keys %$host_selection))
  {
    my $host = encode_entities ($_);
    my $selected = $host_selection->{$_}
    ? ' selected="selected"'
    : '';
    print qq#          <option value="$host"$selected>$host</option>\n#;
  }
  print <<HTML;
        </select>
	<select name="plugin" multiple="multiple" size="15">
HTML
  for (sort (keys %$plugin_selection))
  {
    my $plugin = encode_entities ($_);
    my $selected = $plugin_selection->{$_}
    ? ' selected="selected"'
    : '';
    print qq#          <option value="$plugin"$selected>$plugin</option>\n#;
  }
  print <<HTML;
	</select>
	<select name="timespan">
HTML
  for (sort { $TimeSpans->{$a} <=> $TimeSpans->{$b} } (keys (%$TimeSpans)))
  {
    my $name = encode_entities ($_);
    my $value = $TimeSpans->{$_};
    my $selected = ($value == $timespan_selection)
    ? ' selected="selected"'
    : '';
    print qq#          <option value="$value"$selected>$name</option>\n#;
  }
  print <<HTML;
        </select>
	<input type="hidden" name="action" value="show_selection" />
	<input type="submit" name="ok_button" value="OK" />
      </fieldset>
    </form>
HTML
} # show_selector

sub action_list_hosts
{
  start_html ();
  show_selector ();

  my @hosts = get_all_hosts ();
  print "    <ul>\n";
  for (sort @hosts)
  {
    my $url = encode_entities (script_name () . "?action=show_selection;hostname=$_");
    my $name = encode_entities ($_);
    print qq#      <li><a href="$url">$name</a></li>\n#;
  }
  print "    </ul>\n";

  end_html ();
} # action_list_hosts

=head1 MODULE LOADING

This script makes use of the various B<Collectd::Graph::Type::*> modules. If a
file like C<foo.rrd> is encountered it tries to load the
B<Collectd::Graph::Type::Foo> module and, if that fails, falls back to the
B<Collectd::Graph::Type> base class.

If you want to create a specialized graph for a certain type, you have to
create a new module which inherits from the B<Collectd::Graph::Type> base
class. A description of provided (and used) methods can be found in the inline
documentation of the B<Collectd::Graph::Type> module.

There are other, more specialized, "abstract" classes that possibly better fit
your need. Unfortunately they are not yet documented.

=over 4

=item B<Collectd::Graph::Type::GenericStacked>

Specialized class that groups files by their plugin instance and stacks them on
top of each other. Example types that inherit from this class are
B<Collectd::Graph::Type::Cpu> and B<Collectd::Graph::Type::Memory>.

=item B<Collectd::Graph::Type::GenericIO>

Specialized class for input/output graphs. This class can only handle files
with exactly two data sources, input and output. Example types that inherit
from this class are B<Collectd::Graph::Type::DiskOctets> and
B<Collectd::Graph::Type::IfOctets>.

=back

=cut

sub action_show_selection
{
  start_html ();
  show_selector ();

  my $all_files;
  my $timespan;

  my $types = {};

  my $id_counter = 0;

  $all_files = get_selected_files ();
  $timespan = get_timespan_selection ();

  if (param ('debug'))
  {
    print "<pre>", Data::Dumper->Dump ([$all_files], ['all_files']), "</pre>\n";
  }

  # Send FLUSH command to the daemon if necessary and possible.
  flush_files ($all_files,
      begin => time () - $timespan,
      end => time (),
      addr => gc_get_scalar ('UnixSockAddr', undef),
      interval => gc_get_scalar ('Interval', 10));

  for (@$all_files)
  {
    my $file = $_;
    my $type = ucfirst (lc ($file->{'type'}));

    $type =~ s/[^A-Za-z0-9_]//g;
    $type =~ s/_([A-Za-z0-9])/\U$1\E/g;

    if (!defined ($types->{$type}))
    {
      $types->{$type} = tl_load_type ($file->{'type'});
      if (!$types->{$type})
      {
        warn ("tl_load_type (" . $file->{'type'} . ") failed");
        next;
      }
    }

    $types->{$type}->addFiles ($file);
  }
#print STDOUT Data::Dumper->Dump ([$types], ['types']);

  print qq#    <table>\n#;
  for (sort (keys %$types))
  {
    my $type = $_;

    if (!defined ($types->{$type}))
    {
      next;
    }

    my $graphs_num = $types->{$type}->getGraphsNum ();

    for (my $i = 0; $i < $graphs_num; $i++)
    {
      my $args = $types->{$type}->getGraphArgs ($i);
      my $url = encode_entities ("graph.cgi?$args;begin=-$timespan");
      my $id = sprintf ("graph%04i", $id_counter++);

      print "      <tr>\n";
      print "        <td rowspan=\"$graphs_num\">$type</td>\n" if ($i == 0);
      print <<EOF;
        <td>
          <div class="graph_canvas">
            <div class="graph_float">
              <img id="${id}" class="graph_image"
                alt="A graph"
                src="$url" />
              <div class="controls zoom">
                <div title="Earlier"
                  onclick="nav_move_earlier ('${id}');">&#x2190;</div>
                <div title="Zoom out"
                  onclick="nav_zoom_out ('${id}');">-</div>
                <div title="Zoom in"
                  onclick="nav_zoom_in ('${id}');">+</div>
                <div title="Later"
                  onclick="nav_move_later ('${id}');">&#x2192;</div>
              </div>
              <div class="controls preset">
                <div title="Show current hour"
                  onclick="nav_time_reset ('${id}', 3600);">H</div>
                <div title="Show current day"
                  onclick="nav_time_reset ('${id}', 86400);">D</div>
                <div title="Show current week"
                  onclick="nav_time_reset ('${id}', 7 * 86400);">W</div>
                <div title="Show current month"
                  onclick="nav_time_reset ('${id}', 31 * 86400);">M</div>
                <div title="Show current year"
                  onclick="nav_time_reset ('${id}', 366 * 86400);">Y</div>
                <div title="Set all images to this timespan"
                  onclick="nav_set_reference ('${id}');">!</div>
              </div>
            </div>
          </div>
	</td>
EOF
      # print qq#        <td><img src="$url" /></td>\n#;
      print "      </tr>\n";
    }
  }

  print "    </table>\n";
  end_html ();
}

main ();

=head1 SEE ALSO

L<Collectd::Graph::Type>

=head1 AUTHOR AND LICENSE

Copyright (c) 2008 by Florian Forster
E<lt>octoE<nbsp>atE<nbsp>verplant.orgE<gt>. Licensed under the terms of the GNU
General Public License, VersionE<nbsp>2 (GPLv2).

=cut

# vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
