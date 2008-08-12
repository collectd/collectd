#!/usr/bin/perl

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
use lib ('../lib');
use utf8;

use FindBin ('$RealBin');
use CGI (':cgi');
use CGI::Carp ('fatalsToBrowser');
use URI::Escape ('uri_escape');

use Data::Dumper;

use Collectd::Graph::Config (qw(gc_read_config));
use Collectd::Graph::TypeLoader (qw(tl_load_type));
use Collectd::Graph::Common (qw(get_all_hosts get_files_for_host type_to_module_name));
use Collectd::Graph::Type ();

our $Debug = param ('debug') ? 1 : 0;

gc_read_config ("$RealBin/../etc/collection.conf");

if ($Debug)
{
  print "Content-Type: text/plain; charset=utf-8\n\n";
}
else
{
  print "Content-Type: application/json; charset=utf-8\n\n";
}

print "{\n";

my @hosts = get_all_hosts ();
for (my $i = 0; $i < @hosts; $i++)
{
  my $host = $hosts[$i];
  my $files = get_files_for_host ($host);
  my %graphs = ();
  my @graphs = ();

  # Group files by graphs
  for (@$files)
  {
    my $file = $_;
    my $type = $file->{'type'};

    # Create a new graph object if this is the first of this type.
    if (!defined ($graphs{$type}))
    {
      $graphs{$type} = tl_load_type ($file->{'type'});
      if (!$graphs{$type})
      {
        cluck ("tl_load_type (" . $file->{'type'} . ") failed");
        next;
      }
    }

    $graphs{$type}->addFiles ($file);
  } # for (@$files)

  print qq(  "$host":\n  {\n);

  @graphs = keys %graphs;
  for (my $j = 0; $j < @graphs; $j++)
  {
    my $type = $graphs[$j];
    my $graphs_num = $graphs{$type}->getGraphsNum ();
    my @args = ();

    for (my $k = 0; $k < $graphs_num; $k++)
    {
      my $args = $graphs{$type}->getGraphArgs ($k);
      my $url = 'http://' . $ENV{'SERVER_NAME'} . "/cgi-bin/graph.cgi?" . $args;
      push (@args, $url);
    }

    print qq(    "$type": [ )
    . join (', ', map { qq("$_") } (@args));

    if ($j == (@graphs - 1))
    {
      print qq( ]\n);
    }
    else
    {
      print qq( ],\n);
    }
  } # for (keys %graphs)

  if ($i == (@hosts - 1))
  {
    print qq(  }\n);
  }
  else
  {
    print qq(  },\n\n);
  }
} # for (my $i = 0; $i < @hosts; $i++)

print "}\n";

exit (0);

# vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
