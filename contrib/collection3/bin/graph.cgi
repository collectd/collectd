#!/usr/bin/perl

use strict;
use warnings;
use lib ('../lib');

use FindBin ('$RealBin');
use Carp (qw(confess cluck));
use CGI (':cgi');
use RRDs ();

use Collectd::Graph::TypeLoader (qw(tl_read_config tl_load_type));

use Collectd::Graph::Common (qw(sanitize_type get_selected_files
      epoch_to_rfc1123));
use Collectd::Graph::Type ();

our $Debug = param ('debug');
our $Begin = param ('begin');
our $End = param ('end');
our $GraphWidth = param ('width');

if ($Debug)
{
  print <<HTTP;
Content-Type: text/plain

HTTP
}

tl_read_config ("$RealBin/../etc/collection.conf");

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
if ($Debug)
{
  require Data::Dumper;
  print STDOUT Data::Dumper->Dump ([$files], ['files']);
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

print STDOUT header (-Content_type => 'image/png',
  -Last_Modified => epoch_to_rfc1123 ($obj->getLastModified ()),
  -Expires => epoch_to_rfc1123 ($expires));

if ($Debug)
{
  print "\$expires = $expires;\n";
}

my $args = $obj->getRRDArgs (0);

if ($Debug)
{
  require Data::Dumper;
  print STDOUT Data::Dumper->Dump ([$obj], ['obj']);
  print STDOUT join (",\n", @$args) . "\n";
  print STDOUT "Last-Modified: " . epoch_to_rfc1123 ($obj->getLastModified ()) . "\n";
}
else
{
  my @timesel = ();

  if ($End) # $Begin is always true
  {
    @timesel = ('-s', $Begin, '-e', $End);
  }
  else
  {
    @timesel = ('-s', $Begin); # End is implicitely `now'.
  }

  $| = 1;
  RRDs::graph ('-', '-a', 'PNG', @timesel, @$args);
  if (my $err = RRDs::error ())
  {
    print STDERR "RRDs::graph failed: $err\n";
    exit (1);
  }
}

exit (0);

# vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
