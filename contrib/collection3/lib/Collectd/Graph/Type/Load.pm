package Collectd::Graph::Type::Load;

use strict;
use warnings;
use base ('Collectd::Graph::Type');

use Collectd::Graph::Common (qw($ColorCanvas ident_to_filename get_faded_color));

return (1);

sub new
{
  my $pkg = shift;
  my $obj = Collectd::Graph::Type->new (@_);
  $obj->{'data_sources'} = [qw(shortterm midterm longterm)];
  $obj->{'rrd_opts'} = ['-v', 'System load'];
  $obj->{'rrd_title'} = 'System load';
  $obj->{'rrd_format'} = '%.2lf';
  $obj->{'colors'} = [qw(00ff00 0000ff ff0000)];

  return (bless ($obj, $pkg));
} # new

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
  $filename =~ s#:#\\:#g;

  my $faded_green = get_faded_color ('00ff00');

  return (['-t', 'System load', '-v', 'System load',
    "DEF:s_min=${filename}:shortterm:MIN",
    "DEF:s_avg=${filename}:shortterm:AVERAGE",
    "DEF:s_max=${filename}:shortterm:MAX",
    "DEF:m_min=${filename}:midterm:MIN",
    "DEF:m_avg=${filename}:midterm:AVERAGE",
    "DEF:m_max=${filename}:midterm:MAX",
    "DEF:l_min=${filename}:longterm:MIN",
    "DEF:l_avg=${filename}:longterm:AVERAGE",
    "DEF:l_max=${filename}:longterm:MAX",
    "AREA:s_max#${faded_green}",
    "AREA:s_min#${ColorCanvas}",
    "LINE1:s_avg#00ff00: 1 min",
    "GPRINT:s_min:MIN:%.2lf Min,",
    "GPRINT:s_avg:AVERAGE:%.2lf Avg,",
    "GPRINT:s_max:MAX:%.2lf Max,",
    "GPRINT:s_avg:LAST:%.2lf Last\\l",
    "LINE1:m_avg#0000ff: 5 min",
    "GPRINT:m_min:MIN:%.2lf Min,",
    "GPRINT:m_avg:AVERAGE:%.2lf Avg,",
    "GPRINT:m_max:MAX:%.2lf Max,",
    "GPRINT:m_avg:LAST:%.2lf Last\\l",
    "LINE1:l_avg#ff0000:15 min",
    "GPRINT:l_min:MIN:%.2lf Min,",
    "GPRINT:l_avg:AVERAGE:%.2lf Avg,",
    "GPRINT:l_max:MAX:%.2lf Max,",
    "GPRINT:l_avg:LAST:%.2lf Last\\l"]);
} # sub getRRDArgs

# vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
