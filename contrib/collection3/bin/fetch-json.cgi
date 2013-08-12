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
use Carp (qw(confess cluck));
use CGI (':cgi');
use CGI::Carp ('fatalsToBrowser');
use URI::Escape ('uri_escape');
use RRDs ();

use Data::Dumper;

our $Debug = param ('debug') ? 1 : 0;
our $File = param ('file');
our $Begin = param ('begin');
our $End = param ('end');

confess ('no file given') if (!$File);
if (!($File =~ m#^[^\./][^/]*/[^\./][^/]*/[^\./][^/]*$#))
{
  confess ("Invalid file: $File");
}

if ($Debug)
{
  print "Content-Type: text/plain; charset=utf-8\n\n";
}
else
{
  print "Content-Type: application/json; charset=utf-8\n\n";
}

print "{\n";

{
  my ($start,$step,$names,$data) = RRDs::fetch ("/var/lib/collectd/rrd/$File.rrd", 'AVERAGE', '-s', $Begin, '-e', $End);
  print <<EOF;
  "start": "$start",
  "step":  "$step",
EOF
  print '  "names": ["', join ('", "', @$names), qq("],\n);
  print qq(  "data":\n  [\n);
  for (my $i = 0; $i < @$data; $i++)
  {
    my $row = $data->[$i];
    if ($i > 0)
    {
      print ",\n";
    }
    print qq(    [), join (', ', map { (!defined ($_) || ($_ eq '')) ? 'null' : sprintf ('%.10e', 0.0 + $_) } (@$row)), qq(]);
  }
  print "\n  ]\n";
  #print Data::Dumper->Dump ([$start, $step, $names, $data], [qw(start step names data)]);
}

print "}\n";

exit (0);

# vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
