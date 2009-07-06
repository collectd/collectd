#!/usr/bin/perl

################################################################################
#
# collectd2html.pl
#
# Description:
#   Generate an html page with all rrd data gathered by collectd.
#
# Usage:
#   collectd2html.pl
#
#   When run on <host>, it generated <host>.html and <host>.dir, the latter
#   containing all necessary images.
#
#
# Copyright 2006 Vincent Stehlé <vincent.stehle@free.fr>
#
# Patch to configure the data directory and hostname by Eddy Petrisor
# <eddy.petrisor@gmail.com>.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
################################################################################

use warnings;
use strict;
use Fatal qw(open close);
use File::Basename;
use Getopt::Long qw(:config no_ignore_case bundling pass_through);

my $DIR       = "/var/lib/collectd";
my $HOST      = undef;
my $IMG_FMT   = "PNG";
my $RECURSIVE = 0;

GetOptions (
    "host=s"         => \$HOST,
    "data-dir=s"     => \$DIR,
    "image-format=s" => \$IMG_FMT,
    "recursive"      => \$RECURSIVE
);

if (($DIR !~ m/\/rrd\/?$/) && (-d "$DIR/rrd")) {
	$DIR .= "/rrd";
}

if (defined($HOST) && ($DIR !~ m/\/$HOST\/?$/) && (-d "$DIR/$HOST")) {
	$DIR .= "/$HOST";
}

my @COLORS = (0xff7777, 0x7777ff, 0x55ff55, 0xffcc77, 0xff77ff, 0x77ffff,
	0xffff77, 0x55aaff);
my @tmp = `/bin/hostname`; chomp(@tmp);
$HOST = $tmp[0] if (! defined $HOST);
my $svg_p = ($IMG_FMT eq "SVG");
my $IMG_SFX = $svg_p ? ".svg" : ".png";
my $IMG_DIR = "${HOST}.dir";
my $HTML = "${HOST}.html";

################################################################################
#
# fade_component
#
# Description:
#   Fade a color's component to the white.
#
################################################################################
sub fade_component($)
{
	my($component) = @_;
	return (($component + 255 * 5) / 6);
}

################################################################################
#
# fade_color
#
# Description:
#   Fade a color to the white.
#
################################################################################
sub fade_color($)
{
	my($color) = @_;
	my $r = 0;

	for my $i (0 .. 2){
		my $shft = ($i * 8);
		my $component = (($color >> $shft) & 255);
		$r |= (fade_component($component) << $shft);
	}

	return $r;
}

################################################################################
#
# main
#
################################################################################
system("rm -fR $IMG_DIR");
system("mkdir -p $IMG_DIR");
local *OUT;
open(OUT, ">$HTML");
my $title="Rrd plot for $HOST";

print OUT <<END;
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN"
   "http://www.w3.org/TR/html4/loose.dtd">
<html>
<head>
<title>$title</title>
</head>
<body>
<center>
END

# list interesting rrd
my @rrds;
my @list;

if ($RECURSIVE) {
	@list = `find $DIR -type f -name '*.rrd'`;
}
else {
	@list = `ls $DIR/*.rrd`;
}
chomp(@list);

foreach my $rrd (sort @list){
	$rrd =~ m/^$DIR\/(.*)\.rrd$/;
	push(@rrds, $1);
}

# table of contents
print OUT <<END;
<A name="top"></A><H1>$title</H1>
<P>
END

foreach my $bn (@rrds){
	my $cleaned_bn = $bn;
	$cleaned_bn =~ tr/%\//__/;
	print OUT <<END;
<A href="#$cleaned_bn">$bn</A>
END
}

print OUT <<END;
</P>
END

# graph interesting rrd
for (my $i = 0; $i < scalar(@rrds); ++$i) {
	my $bn = $rrds[$i];
	print "$bn\n";

	my $rrd = $list[$i];
	my $cmd = "rrdtool info $rrd |grep 'ds\\[' |sed 's/^ds\\[//'" 
		." |sed 's/\\].*//' |sort |uniq";
	my @dss = `$cmd`; chomp(@dss);

	# all DEF
	my $j = 0;
	my $defs = "";

	foreach my $ds (@dss){
		$defs .= " DEF:${ds}_avg=$rrd:$ds:AVERAGE"
			." DEF:${ds}_max=$rrd:$ds:MAX ";
	}

	# all AREA
	$j = 0;

	foreach my $ds (@dss){
		my $color = $COLORS[$j % scalar(@COLORS)]; $j++;
		my $faded_color = fade_color($color);
		$defs .= sprintf(" AREA:${ds}_max#%06x ", $faded_color);
	}

	# all LINE	
	$j = 0;

	foreach my $ds (@dss){
		my $color = $COLORS[$j % scalar(@COLORS)]; $j++;
		$defs .= sprintf(" LINE2:${ds}_avg#%06x:$ds"
			." GPRINT:${ds}_avg:AVERAGE:%%5.1lf%%sAvg"
			." GPRINT:${ds}_max:MAX:%%5.1lf%%sMax"
			, $color);
	}

	my $cleaned_bn = $bn;
	$cleaned_bn =~ tr/%\//__/;
	print OUT <<END;
<A name="$cleaned_bn"></A><H1>$bn</H1>
END

	# graph various ranges
	foreach my $span qw(1hour 1day 1week 1month){
		system("mkdir -p $IMG_DIR/" . dirname($bn));
		my $img = "$IMG_DIR/${bn}-$span$IMG_SFX";

		my $cmd = "rrdtool graph $img"
			." -t \"$bn $span\" --imgformat $IMG_FMT --width 600 --height 100"
			." --start now-$span --end now --interlaced"
			." $defs >/dev/null 2>&1";
		system($cmd);

		my $cleaned_img = $img; $cleaned_img =~ s/%/%25/g;
		if (! $svg_p) {
			print OUT <<END;
<P><IMG src="$cleaned_img" alt="${bn} $span"></P>
END
		} else {
			print OUT <<END;
<P><object data="$cleaned_img" type="image/svg+xml"
           width="670" height="179">
  ${bn} $span</object></P>
END
		}
	}

	print OUT <<END;
<A href="#top">[top]</A>
END
}

print OUT <<END;
</center>
</body>
</html>
END

close(OUT);
