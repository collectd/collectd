#!/usr/bin/perl

use strict;
use warnings;

no warnings ('qw');

use CGI;
use RRDs;
use Fcntl (qw(:flock));
use Carp (qw(carp cluck confess croak));

our $Config = read_config ();

our $AbsDir;
our $RelDir;
our $Type;
our $Inst;
our $TimeSpan;

for (qw(Red Green Blue Yellow Cyan Magenta))
{
	$Config->{'Colors'}{"Half$_"} = color_calculate_transparent ($Config->{'Colors'}{'Alpha'},
		$Config->{'Colors'}{'Canvas'}, $Config->{'Colors'}{"Full$_"});
}

$Config->{'Colors'}{'HalfBlueGreen'} = color_calculate_transparent ($Config->{'Colors'}{'Alpha'},
	$Config->{'Colors'}{'Canvas'}, $Config->{'Colors'}{'FullGreen'}, $Config->{'Colors'}{'FullBlue'});
$Config->{'Colors'}{'HalfRedBlue'} = color_calculate_transparent ($Config->{'Colors'}{'Alpha'},
	$Config->{'Colors'}{'Canvas'}, $Config->{'Colors'}{'FullBlue'}, $Config->{'Colors'}{'FullRed'});

our $GraphDefs;
{
	my $Alpha  = $Config->{'Colors'}{'Alpha'};
	my $Canvas = $Config->{'Colors'}{'Canvas'};

	my $FullRed    = $Config->{'Colors'}{'FullRed'};
	my $FullGreen  = $Config->{'Colors'}{'FullGreen'};
	my $FullBlue   = $Config->{'Colors'}{'FullBlue'};
	my $FullYellow = $Config->{'Colors'}{'FullYellow'};
	my $FullCyan   = $Config->{'Colors'}{'FullCyan'};
	my $FullMagenta= $Config->{'Colors'}{'FullMagenta'};

	my $HalfRed    = $Config->{'Colors'}{'HalfRed'};
	my $HalfGreen  = $Config->{'Colors'}{'HalfGreen'};
	my $HalfBlue   = $Config->{'Colors'}{'HalfBlue'};
	my $HalfYellow = $Config->{'Colors'}{'HalfYellow'};
	my $HalfCyan   = $Config->{'Colors'}{'HalfCyan'};
	my $HalfMagenta= $Config->{'Colors'}{'HalfMagenta'};

	my $HalfBlueGreen = $Config->{'Colors'}{'HalfBlueGreen'};
	my $HalfRedBlue   = $Config->{'Colors'}{'HalfRedBlue'};
	
	$GraphDefs =
	{
		apache_bytes => ['DEF:min_raw={file}:count:MIN',
			'DEF:avg_raw={file}:count:AVERAGE',
			'DEF:max_raw={file}:count:MAX',
			'CDEF:min=min_raw,8,*',
			'CDEF:avg=avg_raw,8,*',
			'CDEF:max=max_raw,8,*',
			'CDEF:mytime=avg_raw,TIME,TIME,IF',
			'CDEF:sample_len_raw=mytime,PREV(mytime),-',
			'CDEF:sample_len=sample_len_raw,UN,0,sample_len_raw,IF',
			'CDEF:avg_sample=avg_raw,UN,0,avg_raw,IF,sample_len,*',
			'CDEF:avg_sum=PREV,UN,0,PREV,IF,avg_sample,+',
			"AREA:avg#$HalfBlue",
			"LINE1:avg#$FullBlue:Bit/s",
			'GPRINT:min:MIN:%5.1lf%s Min,',
			'GPRINT:avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:max:MAX:%5.1lf%s Max,',
			'GPRINT:avg:LAST:%5.1lf%s Last',
			'GPRINT:avg_sum:LAST:(ca. %5.1lf%sB Total)\l'
		],
		apache_requests => ['DEF:min={file}:count:MIN',
			'DEF:avg={file}:count:AVERAGE',
			'DEF:max={file}:count:MAX',
			"AREA:max#$HalfBlue",
			"AREA:min#$Canvas",
			"LINE1:avg#$FullBlue:Requests/s",
			'GPRINT:min:MIN:%6.2lf Min,',
			'GPRINT:avg:AVERAGE:%6.2lf Avg,',
			'GPRINT:max:MAX:%6.2lf Max,',
			'GPRINT:avg:LAST:%6.2lf Last'
		],
		apache_scoreboard => ['DEF:min={file}:count:MIN',
			'DEF:avg={file}:count:AVERAGE',
			'DEF:max={file}:count:MAX',
			"AREA:max#$HalfBlue",
			"AREA:min#$Canvas",
			"LINE1:avg#$FullBlue:Processes",
			'GPRINT:min:MIN:%6.2lf Min,',
			'GPRINT:avg:AVERAGE:%6.2lf Avg,',
			'GPRINT:max:MAX:%6.2lf Max,',
			'GPRINT:avg:LAST:%6.2lf Last'
		],
		charge => [
			'DEF:avg={file}:charge:AVERAGE',
			'DEF:min={file}:charge:MIN',
			'DEF:max={file}:charge:MAX',
			"AREA:max#$HalfBlue",
			"AREA:min#$Canvas",
			"LINE1:avg#$FullBlue:Charge",
			'GPRINT:min:MIN:%5.1lf%sAh Min,',
			'GPRINT:avg:AVERAGE:%5.1lf%sAh Avg,',
			'GPRINT:max:MAX:%5.1lf%sAh Max,',
			'GPRINT:avg:LAST:%5.1lf%sAh Last\l'
		],
		charge_percent => [
			'DEF:avg={file}:percent:AVERAGE',
			'DEF:min={file}:percent:MIN',
			'DEF:max={file}:percent:MAX',
			"AREA:max#$HalfBlue",
			"AREA:min#$Canvas",
			"LINE1:avg#$FullBlue:Charge",
			'GPRINT:min:MIN:%5.1lf%s%% Min,',
			'GPRINT:avg:AVERAGE:%5.1lf%s%% Avg,',
			'GPRINT:max:MAX:%5.1lf%s%% Max,',
			'GPRINT:avg:LAST:%5.1lf%s%% Last\l'
		],
		cpu => ['DEF:user_avg={file}:user:AVERAGE',
			'DEF:user_min={file}:user:MIN',
			'DEF:user_max={file}:user:MAX',
			'DEF:nice_avg={file}:nice:AVERAGE',
			'DEF:nice_min={file}:nice:MIN',
			'DEF:nice_max={file}:nice:MAX',
			'DEF:syst_avg={file}:syst:AVERAGE',
			'DEF:syst_min={file}:syst:MIN',
			'DEF:syst_max={file}:syst:MAX',
			'DEF:idle_avg={file}:idle:AVERAGE',
			'DEF:idle_min={file}:idle:MIN',
			'DEF:idle_max={file}:idle:MAX',
			'DEF:wait_avg={file}:wait:AVERAGE',
			'DEF:wait_min={file}:wait:MIN',
			'DEF:wait_max={file}:wait:MAX',
			'CDEF:user_avg_notnull=user_avg,UN,0,user_avg,IF',
			'CDEF:nice_avg_notnull=nice_avg,UN,0,nice_avg,IF',
			'CDEF:syst_avg_notnull=syst_avg,UN,0,syst_avg,IF',
			'CDEF:idle_avg_notnull=idle_avg,UN,0,idle_avg,IF',
			'CDEF:wait_avg_notnull=wait_avg,UN,0,wait_avg,IF',
			'CDEF:totl_avg_notnull=user_avg_notnull,nice_avg_notnull,+,syst_avg_notnull,+,idle_avg_notnull,+,wait_avg_notnull,+',
			'CDEF:user_avg_pct=user_avg_notnull,100,*,totl_avg_notnull,/',
			'CDEF:nice_avg_pct=nice_avg_notnull,100,*,totl_avg_notnull,/',
			'CDEF:syst_avg_pct=syst_avg_notnull,100,*,totl_avg_notnull,/',
			'CDEF:wait_avg_pct=wait_avg_notnull,100,*,totl_avg_notnull,/',
			'CDEF:nice_acc=syst_avg_pct,wait_avg_pct,user_avg_pct,nice_avg_pct,+,+,+',
			'CDEF:user_acc=syst_avg_pct,wait_avg_pct,user_avg_pct,+,+',
			'CDEF:wait_acc=syst_avg_pct,wait_avg_pct,+',
			'CDEF:syst_acc=syst_avg_pct',
#			'CDEF:nice_acc=syst_avg_notnull,wait_avg_notnull,user_avg_notnull,nice_avg_notnull,+,+,+',
#			'CDEF:user_acc=syst_avg_notnull,wait_avg_notnull,user_avg_notnull,+,+',
#			'CDEF:wait_acc=syst_avg_notnull,wait_avg_notnull,+',
#			'CDEF:syst_acc=syst_avg_notnull',
			"AREA:nice_acc#$HalfGreen",
			"AREA:user_acc#$HalfBlue",
			"AREA:wait_acc#$HalfYellow",
			"AREA:syst_acc#$HalfRed",
			"LINE1:nice_acc#$FullGreen:Nice   ",
			'GPRINT:nice_min:MIN:%5.1lf%% Min,',
			'GPRINT:nice_avg:AVERAGE:%5.1lf%% Avg,',
			'GPRINT:nice_max:MAX:%5.1lf%% Max,',
			'GPRINT:nice_avg:LAST:%5.1lf%% Last\l',
			"LINE1:user_acc#$FullBlue:User   ",
			'GPRINT:user_min:MIN:%5.1lf%% Min,',
			'GPRINT:user_avg:AVERAGE:%5.1lf%% Avg,',
			'GPRINT:user_max:MAX:%5.1lf%% Max,',
			'GPRINT:user_avg:LAST:%5.1lf%% Last\l',
			"LINE1:wait_acc#$FullYellow:Wait-IO",
			'GPRINT:wait_min:MIN:%5.1lf%% Min,',
			'GPRINT:wait_avg:AVERAGE:%5.1lf%% Avg,',
			'GPRINT:wait_max:MAX:%5.1lf%% Max,',
			'GPRINT:wait_avg:LAST:%5.1lf%% Last\l',
			"LINE1:syst_acc#$FullRed:System ",
			'GPRINT:syst_min:MIN:%5.1lf%% Min,',
			'GPRINT:syst_avg:AVERAGE:%5.1lf%% Avg,',
			'GPRINT:syst_max:MAX:%5.1lf%% Max,',
			'GPRINT:syst_avg:LAST:%5.1lf%% Last\l'
		],
		current => [
			'DEF:avg={file}:current:AVERAGE',
			'DEF:min={file}:current:MIN',
			'DEF:max={file}:current:MAX',
			"AREA:max#$HalfBlue",
			"AREA:min#$Canvas",
			"LINE1:avg#$FullBlue:Current",
			'GPRINT:min:MIN:%5.1lf%sA Min,',
			'GPRINT:avg:AVERAGE:%5.1lf%sA Avg,',
			'GPRINT:max:MAX:%5.1lf%sA Max,',
			'GPRINT:avg:LAST:%5.1lf%sA Last\l'
		],
		df => [
			'DEF:free_avg={file}:free:AVERAGE',
			'DEF:free_min={file}:free:MIN',
			'DEF:free_max={file}:free:MAX',
			'DEF:used_avg={file}:used:AVERAGE',
			'DEF:used_min={file}:used:MIN',
			'DEF:used_max={file}:used:MAX',
			'CDEF:total=free_avg,used_avg,+',
			'CDEF:free_pct=100,free_avg,*,total,/',
			'CDEF:used_pct=100,used_avg,*,total,/',
			'CDEF:free_acc=free_pct,used_pct,+',
			'CDEF:used_acc=used_pct',
			"AREA:free_acc#$HalfGreen",
			"AREA:used_acc#$HalfRed",
			"LINE1:free_acc#$FullGreen:Free",
			'GPRINT:free_min:MIN:%5.1lf%sB Min,',
			'GPRINT:free_avg:AVERAGE:%5.1lf%sB Avg,',
			'GPRINT:free_max:MAX:%5.1lf%sB Max,',
			'GPRINT:free_avg:LAST:%5.1lf%sB Last\l',
			"LINE1:used_acc#$FullRed:Used",
			'GPRINT:used_min:MIN:%5.1lf%sB Min,',
			'GPRINT:used_avg:AVERAGE:%5.1lf%sB Avg,',
			'GPRINT:used_max:MAX:%5.1lf%sB Max,',
			'GPRINT:used_avg:LAST:%5.1lf%sB Last\l'
		],
		disk => [
			'DEF:rtime_avg={file}:rtime:AVERAGE',
			'DEF:rtime_min={file}:rtime:MIN',
			'DEF:rtime_max={file}:rtime:MAX',
			'DEF:wtime_avg={file}:wtime:AVERAGE',
			'DEF:wtime_min={file}:wtime:MIN',
			'DEF:wtime_max={file}:wtime:MAX',
			'CDEF:rtime_avg_ms=rtime_avg,1000,/',
			'CDEF:rtime_min_ms=rtime_min,1000,/',
			'CDEF:rtime_max_ms=rtime_max,1000,/',
			'CDEF:wtime_avg_ms=wtime_avg,1000,/',
			'CDEF:wtime_min_ms=wtime_min,1000,/',
			'CDEF:wtime_max_ms=wtime_max,1000,/',
			'CDEF:total_avg_ms=rtime_avg_ms,wtime_avg_ms,+',
			'CDEF:total_min_ms=rtime_min_ms,wtime_min_ms,+',
			'CDEF:total_max_ms=rtime_max_ms,wtime_max_ms,+',
			"AREA:total_max_ms#$HalfRed",
			"AREA:total_min_ms#$Canvas",
			"LINE1:wtime_avg_ms#$FullGreen:Write",
			'GPRINT:wtime_min_ms:MIN:%5.1lf%s Min,',
			'GPRINT:wtime_avg_ms:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:wtime_max_ms:MAX:%5.1lf%s Max,',
			'GPRINT:wtime_avg_ms:LAST:%5.1lf%s Last\n',
			"LINE1:rtime_avg_ms#$FullBlue:Read ",
			'GPRINT:rtime_min_ms:MIN:%5.1lf%s Min,',
			'GPRINT:rtime_avg_ms:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:rtime_max_ms:MAX:%5.1lf%s Max,',
			'GPRINT:rtime_avg_ms:LAST:%5.1lf%s Last\n',
			"LINE1:total_avg_ms#$FullRed:Total",
			'GPRINT:total_min_ms:MIN:%5.1lf%s Min,',
			'GPRINT:total_avg_ms:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:total_max_ms:MAX:%5.1lf%s Max,',
			'GPRINT:total_avg_ms:LAST:%5.1lf%s Last'
		],
		fanspeed => [
			'DEF:temp_avg={file}:value:AVERAGE',
			'DEF:temp_min={file}:value:MIN',
			'DEF:temp_max={file}:value:MAX',
			"AREA:temp_max#$HalfBlue",
			"AREA:temp_min#$Canvas",
			"LINE1:temp_avg#$FullBlue:RPM",
			'GPRINT:temp_min:MIN:%4.1lf Min,',
			'GPRINT:temp_avg:AVERAGE:%4.1lf Avg,',
			'GPRINT:temp_max:MAX:%4.1lf Max,',
			'GPRINT:temp_avg:LAST:%4.1lf Last\l'
		],
		frequency_offset => [ # NTPd
			'DEF:ppm_avg={file}:ppm:AVERAGE',
			'DEF:ppm_min={file}:ppm:MIN',
			'DEF:ppm_max={file}:ppm:MAX',
			"AREA:ppm_max#$HalfBlue",
			"AREA:ppm_min#$Canvas",
			"LINE1:ppm_avg#$FullBlue:{inst}",
			'GPRINT:ppm_min:MIN:%5.2lf Min,',
			'GPRINT:ppm_avg:AVERAGE:%5.2lf Avg,',
			'GPRINT:ppm_max:MAX:%5.2lf Max,',
			'GPRINT:ppm_avg:LAST:%5.2lf Last'
		],
		hddtemp => [
			'DEF:temp_avg={file}:value:AVERAGE',
			'DEF:temp_min={file}:value:MIN',
			'DEF:temp_max={file}:value:MAX',
			"AREA:temp_max#$HalfBlue",
			"AREA:temp_min#$Canvas",
			"LINE1:temp_avg#$FullBlue:Temperature",
			'GPRINT:temp_min:MIN:%4.1lf Min,',
			'GPRINT:temp_avg:AVERAGE:%4.1lf Avg,',
			'GPRINT:temp_max:MAX:%4.1lf Max,',
			'GPRINT:temp_avg:LAST:%4.1lf Last\l'
		],
		if_packets => ['DEF:tx_min={file}:tx:MIN',
			'DEF:tx_avg={file}:tx:AVERAGE',
			'DEF:tx_max={file}:tx:MAX',
			'DEF:rx_min={file}:rx:MIN',
			'DEF:rx_avg={file}:rx:AVERAGE',
			'DEF:rx_max={file}:rx:MAX',
			'CDEF:overlap=tx_avg,rx_avg,GT,rx_avg,tx_avg,IF',
			'CDEF:mytime=tx_avg,TIME,TIME,IF',
			'CDEF:sample_len_raw=mytime,PREV(mytime),-',
			'CDEF:sample_len=sample_len_raw,UN,0,sample_len_raw,IF',
			'CDEF:tx_avg_sample=tx_avg,UN,0,tx_avg,IF,sample_len,*',
			'CDEF:tx_avg_sum=PREV,UN,0,PREV,IF,tx_avg_sample,+',
			'CDEF:rx_avg_sample=rx_avg,UN,0,rx_avg,IF,sample_len,*',
			'CDEF:rx_avg_sum=PREV,UN,0,PREV,IF,rx_avg_sample,+',
			"AREA:tx_avg#$HalfGreen",
			"AREA:rx_avg#$HalfBlue",
			"AREA:overlap#$HalfBlueGreen",
			"LINE1:tx_avg#$FullGreen:TX",
			'GPRINT:tx_avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:tx_max:MAX:%5.1lf%s Max,',
			'GPRINT:tx_avg:LAST:%5.1lf%s Last',
			'GPRINT:tx_avg_sum:LAST:(ca. %4.0lf%s Total)\l',
			"LINE1:rx_avg#$FullBlue:RX",
			#'GPRINT:rx_min:MIN:%5.1lf %s Min,',
			'GPRINT:rx_avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:rx_max:MAX:%5.1lf%s Max,',
			'GPRINT:rx_avg:LAST:%5.1lf%s Last',
			'GPRINT:rx_avg_sum:LAST:(ca. %4.0lf%s Total)\l'
		],
		load => ['DEF:s_avg={file}:shortterm:AVERAGE',
			'DEF:s_min={file}:shortterm:MIN',
			'DEF:s_max={file}:shortterm:MAX',
			'DEF:m_avg={file}:midterm:AVERAGE',
			'DEF:m_min={file}:midterm:MIN',
			'DEF:m_max={file}:midterm:MAX',
			'DEF:l_avg={file}:longterm:AVERAGE',
			'DEF:l_min={file}:longterm:MIN',
			'DEF:l_max={file}:longterm:MAX',
			"AREA:s_max#$HalfGreen",
			"AREA:s_min#$Canvas",
			"LINE1:s_avg#$FullGreen: 1m average",
			'GPRINT:s_min:MIN:%4.2lf Min,',
			'GPRINT:s_avg:AVERAGE:%4.2lf Avg,',
			'GPRINT:s_max:MAX:%4.2lf Max,',
			'GPRINT:s_avg:LAST:%4.2lf Last\n',
			"LINE1:m_avg#$FullBlue: 5m average",
			'GPRINT:m_min:MIN:%4.2lf Min,',
			'GPRINT:m_avg:AVERAGE:%4.2lf Avg,',
			'GPRINT:m_max:MAX:%4.2lf Max,',
			'GPRINT:m_avg:LAST:%4.2lf Last\n',
			"LINE1:l_avg#$FullRed:15m average",
			'GPRINT:l_min:MIN:%4.2lf Min,',
			'GPRINT:l_avg:AVERAGE:%4.2lf Avg,',
			'GPRINT:l_max:MAX:%4.2lf Max,',
			'GPRINT:l_avg:LAST:%4.2lf Last'
		],
		load_percent => [
			'DEF:avg={file}:percent:AVERAGE',
			'DEF:min={file}:percent:MIN',
			'DEF:max={file}:percent:MAX',
			"AREA:max#$HalfBlue",
			"AREA:min#$Canvas",
			"LINE1:avg#$FullBlue:Load",
			'GPRINT:min:MIN:%5.1lf%s%% Min,',
			'GPRINT:avg:AVERAGE:%5.1lf%s%% Avg,',
			'GPRINT:max:MAX:%5.1lf%s%% Max,',
			'GPRINT:avg:LAST:%5.1lf%s%% Last\l'
		],
		mails => ['DEF:rawgood={file}:good:AVERAGE',
			'DEF:rawspam={file}:spam:AVERAGE',
			'CDEF:good=rawgood,UN,0,rawgood,IF',
			'CDEF:spam=rawspam,UN,0,rawspam,IF',
			'CDEF:negspam=spam,-1,*',
			"AREA:good#$HalfGreen",
			"LINE1:good#$FullGreen:Good mails",
			'GPRINT:good:AVERAGE:%4.1lf Avg,',
			'GPRINT:good:MAX:%4.1lf Max,',
			'GPRINT:good:LAST:%4.1lf Last\n',
			"AREA:negspam#$HalfRed",
			"LINE1:negspam#$FullRed:Spam mails",
			'GPRINT:spam:AVERAGE:%4.1lf Avg,',
			'GPRINT:spam:MAX:%4.1lf Max,',
			'GPRINT:spam:LAST:%4.1lf Last',
			'HRULE:0#000000'],
		memory => [
			'DEF:used_avg={file}:used:AVERAGE',
			'DEF:free_avg={file}:free:AVERAGE',
			'DEF:buffers_avg={file}:buffers:AVERAGE',
			'DEF:cached_avg={file}:cached:AVERAGE',
			'DEF:used_min={file}:used:MIN',
			'DEF:free_min={file}:free:MIN',
			'DEF:buffers_min={file}:buffers:MIN',
			'DEF:cached_min={file}:cached:MIN',
			'DEF:used_max={file}:used:MAX',
			'DEF:free_max={file}:free:MAX',
			'DEF:buffers_max={file}:buffers:MAX',
			'DEF:cached_max={file}:cached:MAX',
			'CDEF:cached_avg_nn=cached_avg,UN,0,cached_avg,IF',
			'CDEF:buffers_avg_nn=buffers_avg,UN,0,buffers_avg,IF',
			'CDEF:free_cached_buffers_used=free_avg,cached_avg_nn,+,buffers_avg_nn,+,used_avg,+',
			'CDEF:cached_buffers_used=cached_avg,buffers_avg_nn,+,used_avg,+',
			'CDEF:buffers_used=buffers_avg,used_avg,+',
			"AREA:free_cached_buffers_used#$HalfGreen",
			"AREA:cached_buffers_used#$HalfBlue",
			"AREA:buffers_used#$HalfYellow",
			"AREA:used_avg#$HalfRed",
			"LINE1:free_cached_buffers_used#$FullGreen:Free        ",
			'GPRINT:free_min:MIN:%5.1lf%s Min,',
			'GPRINT:free_avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:free_max:MAX:%5.1lf%s Max,',
			'GPRINT:free_avg:LAST:%5.1lf%s Last\n',
			"LINE1:cached_buffers_used#$FullBlue:Page cache  ",
			'GPRINT:cached_min:MIN:%5.1lf%s Min,',
			'GPRINT:cached_avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:cached_max:MAX:%5.1lf%s Max,',
			'GPRINT:cached_avg:LAST:%5.1lf%s Last\n',
			"LINE1:buffers_used#$FullYellow:Buffer cache",
			'GPRINT:buffers_min:MIN:%5.1lf%s Min,',
			'GPRINT:buffers_avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:buffers_max:MAX:%5.1lf%s Max,',
			'GPRINT:buffers_avg:LAST:%5.1lf%s Last\n',
			"LINE1:used_avg#$FullRed:Used        ",
			'GPRINT:used_min:MIN:%5.1lf%s Min,',
			'GPRINT:used_avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:used_max:MAX:%5.1lf%s Max,',
			'GPRINT:used_avg:LAST:%5.1lf%s Last'
		],
		mysql_commands => [
			"DEF:val_avg={file}:value:AVERAGE",
			"DEF:val_min={file}:value:MIN",
			"DEF:val_max={file}:value:MAX",
			"AREA:val_max#$HalfBlue",
			"AREA:val_min#$Canvas",
			"LINE1:val_avg#$FullBlue:{inst}",
			'GPRINT:val_min:MIN:%5.2lf Min,',
			'GPRINT:val_avg:AVERAGE:%5.2lf Avg,',
			'GPRINT:val_max:MAX:%5.2lf Max,',
			'GPRINT:val_avg:LAST:%5.2lf Last'
		],
		mysql_handler => [
			"DEF:val_avg={file}:value:AVERAGE",
			"DEF:val_min={file}:value:MIN",
			"DEF:val_max={file}:value:MAX",
			"AREA:val_max#$HalfBlue",
			"AREA:val_min#$Canvas",
			"LINE1:val_avg#$FullBlue:{inst}",
			'GPRINT:val_min:MIN:%5.2lf Min,',
			'GPRINT:val_avg:AVERAGE:%5.2lf Avg,',
			'GPRINT:val_max:MAX:%5.2lf Max,',
			'GPRINT:val_avg:LAST:%5.2lf Last'
		],
		mysql_qcache => [
			"DEF:hits_min={file}:hits:MIN",
			"DEF:hits_avg={file}:hits:AVERAGE",
			"DEF:hits_max={file}:hits:MAX",
			"DEF:inserts_min={file}:inserts:MIN",
			"DEF:inserts_avg={file}:inserts:AVERAGE",
			"DEF:inserts_max={file}:inserts:MAX",
			"DEF:not_cached_min={file}:not_cached:MIN",
			"DEF:not_cached_avg={file}:not_cached:AVERAGE",
			"DEF:not_cached_max={file}:not_cached:MAX",
			"DEF:lowmem_prunes_min={file}:lowmem_prunes:MIN",
			"DEF:lowmem_prunes_avg={file}:lowmem_prunes:AVERAGE",
			"DEF:lowmem_prunes_max={file}:lowmem_prunes:MAX",
			"DEF:queries_min={file}:queries_in_cache:MIN",
			"DEF:queries_avg={file}:queries_in_cache:AVERAGE",
			"DEF:queries_max={file}:queries_in_cache:MAX",
			"CDEF:unknown=queries_avg,UNKN,+",
			"CDEF:not_cached_agg=hits_avg,inserts_avg,+,not_cached_avg,+",
			"CDEF:inserts_agg=hits_avg,inserts_avg,+",
			"CDEF:hits_agg=hits_avg",
			"AREA:not_cached_agg#$HalfYellow",
			"AREA:inserts_agg#$HalfBlue",
			"AREA:hits_agg#$HalfGreen",
			"LINE1:not_cached_agg#$FullYellow:Not Cached      ",
			'GPRINT:not_cached_min:MIN:%5.2lf Min,',
			'GPRINT:not_cached_avg:AVERAGE:%5.2lf Avg,',
			'GPRINT:not_cached_max:MAX:%5.2lf Max,',
			'GPRINT:not_cached_avg:LAST:%5.2lf Last\l',
			"LINE1:inserts_agg#$FullBlue:Inserts         ",
			'GPRINT:inserts_min:MIN:%5.2lf Min,',
			'GPRINT:inserts_avg:AVERAGE:%5.2lf Avg,',
			'GPRINT:inserts_max:MAX:%5.2lf Max,',
			'GPRINT:inserts_avg:LAST:%5.2lf Last\l',
			"LINE1:hits_agg#$FullGreen:Hits            ",
			'GPRINT:hits_min:MIN:%5.2lf Min,',
			'GPRINT:hits_avg:AVERAGE:%5.2lf Avg,',
			'GPRINT:hits_max:MAX:%5.2lf Max,',
			'GPRINT:hits_avg:LAST:%5.2lf Last\l',
			"LINE1:lowmem_prunes_avg#$FullRed:Lowmem Prunes   ",
			'GPRINT:lowmem_prunes_min:MIN:%5.2lf Min,',
			'GPRINT:lowmem_prunes_avg:AVERAGE:%5.2lf Avg,',
			'GPRINT:lowmem_prunes_max:MAX:%5.2lf Max,',
			'GPRINT:lowmem_prunes_avg:LAST:%5.2lf Last\l',
			"LINE1:unknown#$Canvas:Queries in cache",
			'GPRINT:queries_min:MIN:%5.0lf Min,',
			'GPRINT:queries_avg:AVERAGE:%5.0lf Avg,',
			'GPRINT:queries_max:MAX:%5.0lf Max,',
			'GPRINT:queries_avg:LAST:%5.0lf Last\l'
		],
		mysql_threads => [
			"DEF:running_min={file}:running:MIN",
			"DEF:running_avg={file}:running:AVERAGE",
			"DEF:running_max={file}:running:MAX",
			"DEF:connected_min={file}:connected:MIN",
			"DEF:connected_avg={file}:connected:AVERAGE",
			"DEF:connected_max={file}:connected:MAX",
			"DEF:cached_min={file}:cached:MIN",
			"DEF:cached_avg={file}:cached:AVERAGE",
			"DEF:cached_max={file}:cached:MAX",
			"DEF:created_min={file}:created:MIN",
			"DEF:created_avg={file}:created:AVERAGE",
			"DEF:created_max={file}:created:MAX",
			"CDEF:unknown=created_avg,UNKN,+",
			"CDEF:cached_agg=connected_avg,cached_avg,+",
			"AREA:cached_agg#$HalfGreen",
			"AREA:connected_avg#$HalfBlue",
			"AREA:running_avg#$HalfRed",
			"LINE1:cached_agg#$FullGreen:Cached   ",
			'GPRINT:cached_min:MIN:%5.1lf Min,',
			'GPRINT:cached_avg:AVERAGE:%5.1lf Avg,',
			'GPRINT:cached_max:MAX:%5.1lf Max,',
			'GPRINT:cached_avg:LAST:%5.1lf Last\l',
			"LINE1:connected_avg#$FullBlue:Connected",
			'GPRINT:connected_min:MIN:%5.1lf Min,',
			'GPRINT:connected_avg:AVERAGE:%5.1lf Avg,',
			'GPRINT:connected_max:MAX:%5.1lf Max,',
			'GPRINT:connected_avg:LAST:%5.1lf Last\l',
			"LINE1:running_avg#$FullRed:Running  ",
			'GPRINT:running_min:MIN:%5.1lf Min,',
			'GPRINT:running_avg:AVERAGE:%5.1lf Avg,',
			'GPRINT:running_max:MAX:%5.1lf Max,',
			'GPRINT:running_avg:LAST:%5.1lf Last\l',
			"LINE1:unknown#$Canvas:Created  ",
			'GPRINT:created_min:MIN:%5.0lf Min,',
			'GPRINT:created_avg:AVERAGE:%5.0lf Avg,',
			'GPRINT:created_max:MAX:%5.0lf Max,',
			'GPRINT:created_avg:LAST:%5.0lf Last\l'
		],
		nfs3_procedures => [
			"DEF:null_avg={file}:null:AVERAGE",
			"DEF:getattr_avg={file}:getattr:AVERAGE",
			"DEF:setattr_avg={file}:setattr:AVERAGE",
			"DEF:lookup_avg={file}:lookup:AVERAGE",
			"DEF:access_avg={file}:access:AVERAGE",
			"DEF:readlink_avg={file}:readlink:AVERAGE",
			"DEF:read_avg={file}:read:AVERAGE",
			"DEF:write_avg={file}:write:AVERAGE",
			"DEF:create_avg={file}:create:AVERAGE",
			"DEF:mkdir_avg={file}:mkdir:AVERAGE",
			"DEF:symlink_avg={file}:symlink:AVERAGE",
			"DEF:mknod_avg={file}:mknod:AVERAGE",
			"DEF:remove_avg={file}:remove:AVERAGE",
			"DEF:rmdir_avg={file}:rmdir:AVERAGE",
			"DEF:rename_avg={file}:rename:AVERAGE",
			"DEF:link_avg={file}:link:AVERAGE",
			"DEF:readdir_avg={file}:readdir:AVERAGE",
			"DEF:readdirplus_avg={file}:readdirplus:AVERAGE",
			"DEF:fsstat_avg={file}:fsstat:AVERAGE",
			"DEF:fsinfo_avg={file}:fsinfo:AVERAGE",
			"DEF:pathconf_avg={file}:pathconf:AVERAGE",
			"DEF:commit_avg={file}:commit:AVERAGE",
			"DEF:null_max={file}:null:MAX",
			"DEF:getattr_max={file}:getattr:MAX",
			"DEF:setattr_max={file}:setattr:MAX",
			"DEF:lookup_max={file}:lookup:MAX",
			"DEF:access_max={file}:access:MAX",
			"DEF:readlink_max={file}:readlink:MAX",
			"DEF:read_max={file}:read:MAX",
			"DEF:write_max={file}:write:MAX",
			"DEF:create_max={file}:create:MAX",
			"DEF:mkdir_max={file}:mkdir:MAX",
			"DEF:symlink_max={file}:symlink:MAX",
			"DEF:mknod_max={file}:mknod:MAX",
			"DEF:remove_max={file}:remove:MAX",
			"DEF:rmdir_max={file}:rmdir:MAX",
			"DEF:rename_max={file}:rename:MAX",
			"DEF:link_max={file}:link:MAX",
			"DEF:readdir_max={file}:readdir:MAX",
			"DEF:readdirplus_max={file}:readdirplus:MAX",
			"DEF:fsstat_max={file}:fsstat:MAX",
			"DEF:fsinfo_max={file}:fsinfo:MAX",
			"DEF:pathconf_max={file}:pathconf:MAX",
			"DEF:commit_max={file}:commit:MAX",
			"CDEF:other_avg=null_avg,readlink_avg,create_avg,mkdir_avg,symlink_avg,mknod_avg,remove_avg,rmdir_avg,rename_avg,link_avg,readdir_avg,readdirplus_avg,fsstat_avg,fsinfo_avg,pathconf_avg,+,+,+,+,+,+,+,+,+,+,+,+,+,+",
			"CDEF:other_max=null_max,readlink_max,create_max,mkdir_max,symlink_max,mknod_max,remove_max,rmdir_max,rename_max,link_max,readdir_max,readdirplus_max,fsstat_max,fsinfo_max,pathconf_max,+,+,+,+,+,+,+,+,+,+,+,+,+,+",
			"CDEF:stack_read=read_avg",
			"CDEF:stack_getattr=stack_read,getattr_avg,+",
			"CDEF:stack_access=stack_getattr,access_avg,+",
			"CDEF:stack_lookup=stack_access,lookup_avg,+",
			"CDEF:stack_write=stack_lookup,write_avg,+",
			"CDEF:stack_commit=stack_write,commit_avg,+",
			"CDEF:stack_setattr=stack_commit,setattr_avg,+",
			"CDEF:stack_other=stack_setattr,other_avg,+",
			"AREA:stack_other#$HalfRed",
			"AREA:stack_setattr#$HalfGreen",
			"AREA:stack_commit#$HalfYellow",
			"AREA:stack_write#$HalfGreen",
			"AREA:stack_lookup#$HalfBlue",
			"AREA:stack_access#$HalfMagenta",
			"AREA:stack_getattr#$HalfCyan",
			"AREA:stack_read#$HalfBlue",
			"LINE1:stack_other#$FullRed:Other  ",
			'GPRINT:other_max:MAX:%5.1lf Max,',
			'GPRINT:other_avg:AVERAGE:%5.1lf Avg,',
			'GPRINT:other_avg:LAST:%5.1lf Last\l',
			"LINE1:stack_setattr#$FullGreen:setattr",
			'GPRINT:setattr_max:MAX:%5.1lf Max,',
			'GPRINT:setattr_avg:AVERAGE:%5.1lf Avg,',
			'GPRINT:setattr_avg:LAST:%5.1lf Last\l',
			"LINE1:stack_commit#$FullYellow:commit ",
			'GPRINT:commit_max:MAX:%5.1lf Max,',
			'GPRINT:commit_avg:AVERAGE:%5.1lf Avg,',
			'GPRINT:commit_avg:LAST:%5.1lf Last\l',
			"LINE1:stack_write#$FullGreen:write  ",
			'GPRINT:write_max:MAX:%5.1lf Max,',
			'GPRINT:write_avg:AVERAGE:%5.1lf Avg,',
			'GPRINT:write_avg:LAST:%5.1lf Last\l',
			"LINE1:stack_lookup#$FullBlue:lookup ",
			'GPRINT:lookup_max:MAX:%5.1lf Max,',
			'GPRINT:lookup_avg:AVERAGE:%5.1lf Avg,',
			'GPRINT:lookup_avg:LAST:%5.1lf Last\l',
			"LINE1:stack_access#$FullMagenta:access ",
			'GPRINT:access_max:MAX:%5.1lf Max,',
			'GPRINT:access_avg:AVERAGE:%5.1lf Avg,',
			'GPRINT:access_avg:LAST:%5.1lf Last\l',
			"LINE1:stack_getattr#$FullCyan:getattr",
			'GPRINT:getattr_max:MAX:%5.1lf Max,',
			'GPRINT:getattr_avg:AVERAGE:%5.1lf Avg,',
			'GPRINT:getattr_avg:LAST:%5.1lf Last\l',
			"LINE1:stack_read#$FullBlue:read   ",
			'GPRINT:read_max:MAX:%5.1lf Max,',
			'GPRINT:read_avg:AVERAGE:%5.1lf Avg,',
			'GPRINT:read_avg:LAST:%5.1lf Last\l'
		],
		partition => [
			"DEF:rbyte_avg={file}:rbytes:AVERAGE",
			"DEF:rbyte_min={file}:rbytes:MIN",
			"DEF:rbyte_max={file}:rbytes:MAX",
			"DEF:wbyte_avg={file}:wbytes:AVERAGE",
			"DEF:wbyte_min={file}:wbytes:MIN",
			"DEF:wbyte_max={file}:wbytes:MAX",
			'CDEF:overlap=wbyte_avg,rbyte_avg,GT,rbyte_avg,wbyte_avg,IF',
			"AREA:wbyte_avg#$HalfGreen",
			"AREA:rbyte_avg#$HalfBlue",
			"AREA:overlap#$HalfBlueGreen",
			"LINE1:wbyte_avg#$FullGreen:Write",
			'GPRINT:wbyte_min:MIN:%5.1lf%s Min,',
			'GPRINT:wbyte_avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:wbyte_max:MAX:%5.1lf%s Max,',
			'GPRINT:wbyte_avg:LAST:%5.1lf%s Last\l',
			"LINE1:rbyte_avg#$FullBlue:Read ",
			'GPRINT:rbyte_min:MIN:%5.1lf%s Min,',
			'GPRINT:rbyte_avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:rbyte_max:MAX:%5.1lf%s Max,',
			'GPRINT:rbyte_avg:LAST:%5.1lf%s Last\l'
		],
		ping => ['DEF:ping_avg={file}:ping:AVERAGE',
			'DEF:ping_min={file}:ping:MIN',
			'DEF:ping_max={file}:ping:MAX',
			"AREA:ping_max#$HalfBlue",
			"AREA:ping_min#$Canvas",
			"LINE1:ping_avg#$FullBlue:Ping",
			'GPRINT:ping_min:MIN:%4.1lf ms Min,',
			'GPRINT:ping_avg:AVERAGE:%4.1lf ms Avg,',
			'GPRINT:ping_max:MAX:%4.1lf ms Max,',
			'GPRINT:ping_avg:LAST:%4.1lf ms Last'],
		processes => [
			"DEF:running_avg={file}:running:AVERAGE",
			"DEF:running_min={file}:running:MIN",
			"DEF:running_max={file}:running:MAX",
			"DEF:sleeping_avg={file}:sleeping:AVERAGE",
			"DEF:sleeping_min={file}:sleeping:MIN",
			"DEF:sleeping_max={file}:sleeping:MAX",
			"DEF:zombies_avg={file}:zombies:AVERAGE",
			"DEF:zombies_min={file}:zombies:MIN",
			"DEF:zombies_max={file}:zombies:MAX",
			"DEF:stopped_avg={file}:stopped:AVERAGE",
			"DEF:stopped_min={file}:stopped:MIN",
			"DEF:stopped_max={file}:stopped:MAX",
			"DEF:paging_avg={file}:paging:AVERAGE",
			"DEF:paging_min={file}:paging:MIN",
			"DEF:paging_max={file}:paging:MAX",
			"DEF:blocked_avg={file}:blocked:AVERAGE",
			"DEF:blocked_min={file}:blocked:MIN",
			"DEF:blocked_max={file}:blocked:MAX",
			'CDEF:paging_acc=sleeping_avg,running_avg,stopped_avg,zombies_avg,blocked_avg,paging_avg,+,+,+,+,+',
			'CDEF:blocked_acc=sleeping_avg,running_avg,stopped_avg,zombies_avg,blocked_avg,+,+,+,+',
			'CDEF:zombies_acc=sleeping_avg,running_avg,stopped_avg,zombies_avg,+,+,+',
			'CDEF:stopped_acc=sleeping_avg,running_avg,stopped_avg,+,+',
			'CDEF:running_acc=sleeping_avg,running_avg,+',
			'CDEF:sleeping_acc=sleeping_avg',
			"AREA:paging_acc#$HalfYellow",
			"AREA:blocked_acc#$HalfCyan",
			"AREA:zombies_acc#$HalfRed",
			"AREA:stopped_acc#$HalfMagenta",
			"AREA:running_acc#$HalfGreen",
			"AREA:sleeping_acc#$HalfBlue",
			"LINE1:paging_acc#$FullYellow:Paging  ",
			'GPRINT:paging_min:MIN:%5.1lf Min,',
			'GPRINT:paging_avg:AVERAGE:%5.1lf Average,',
			'GPRINT:paging_max:MAX:%5.1lf Max,',
			'GPRINT:paging_avg:LAST:%5.1lf Last\l',
			"LINE1:blocked_acc#$FullCyan:Blocked ",
			'GPRINT:blocked_min:MIN:%5.1lf Min,',
			'GPRINT:blocked_avg:AVERAGE:%5.1lf Average,',
			'GPRINT:blocked_max:MAX:%5.1lf Max,',
			'GPRINT:blocked_avg:LAST:%5.1lf Last\l',
			"LINE1:zombies_acc#$FullRed:Zombies ",
			'GPRINT:zombies_min:MIN:%5.1lf Min,',
			'GPRINT:zombies_avg:AVERAGE:%5.1lf Average,',
			'GPRINT:zombies_max:MAX:%5.1lf Max,',
			'GPRINT:zombies_avg:LAST:%5.1lf Last\l',
			"LINE1:stopped_acc#$FullMagenta:Stopped ",
			'GPRINT:stopped_min:MIN:%5.1lf Min,',
			'GPRINT:stopped_avg:AVERAGE:%5.1lf Average,',
			'GPRINT:stopped_max:MAX:%5.1lf Max,',
			'GPRINT:stopped_avg:LAST:%5.1lf Last\l',
			"LINE1:running_acc#$FullGreen:Running ",
			'GPRINT:running_min:MIN:%5.1lf Min,',
			'GPRINT:running_avg:AVERAGE:%5.1lf Average,',
			'GPRINT:running_max:MAX:%5.1lf Max,',
			'GPRINT:running_avg:LAST:%5.1lf Last\l',
			"LINE1:sleeping_acc#$FullBlue:Sleeping",
			'GPRINT:sleeping_min:MIN:%5.1lf Min,',
			'GPRINT:sleeping_avg:AVERAGE:%5.1lf Average,',
			'GPRINT:sleeping_max:MAX:%5.1lf Max,',
			'GPRINT:sleeping_avg:LAST:%5.1lf Last\l'
		],
		ps_rss => [
			'DEF:avg={file}:byte:AVERAGE',
			'DEF:min={file}:byte:MIN',
			'DEF:max={file}:byte:MAX',
			"AREA:avg#$HalfBlue",
			"LINE1:avg#$FullBlue:RSS",
			'GPRINT:min:MIN:%5.1lf%s Min,',
			'GPRINT:avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:max:MAX:%5.1lf%s Max,',
			'GPRINT:avg:LAST:%5.1lf%s Last\l'
		],
		ps_cputime => [
			'DEF:user_avg_raw={file}:user:AVERAGE',
			'DEF:user_min_raw={file}:user:MIN',
			'DEF:user_max_raw={file}:user:MAX',
			'DEF:syst_avg_raw={file}:syst:AVERAGE',
			'DEF:syst_min_raw={file}:syst:MIN',
			'DEF:syst_max_raw={file}:syst:MAX',
			'CDEF:user_avg=user_avg_raw,1000000,/',
			'CDEF:user_min=user_min_raw,1000000,/',
			'CDEF:user_max=user_max_raw,1000000,/',
			'CDEF:syst_avg=syst_avg_raw,1000000,/',
			'CDEF:syst_min=syst_min_raw,1000000,/',
			'CDEF:syst_max=syst_max_raw,1000000,/',
			'CDEF:user_syst=syst_avg,UN,0,syst_avg,IF,user_avg,+',
			"AREA:user_syst#$HalfBlue",
			"AREA:syst_avg#$HalfRed",
			"LINE1:user_syst#$FullBlue:User  ",
			'GPRINT:user_min:MIN:%5.1lf%s Min,',
			'GPRINT:user_avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:user_max:MAX:%5.1lf%s Max,',
			'GPRINT:user_avg:LAST:%5.1lf%s Last\l',
			"LINE1:syst_avg#$FullRed:System",
			'GPRINT:syst_min:MIN:%5.1lf%s Min,',
			'GPRINT:syst_avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:syst_max:MAX:%5.1lf%s Max,',
			'GPRINT:syst_avg:LAST:%5.1lf%s Last\l'
		],
		ps_count => [
			'DEF:procs_avg={file}:processes:AVERAGE',
			'DEF:procs_min={file}:processes:MIN',
			'DEF:procs_max={file}:processes:MAX',
			'DEF:thrds_avg={file}:threads:AVERAGE',
			'DEF:thrds_min={file}:threads:MIN',
			'DEF:thrds_max={file}:threads:MAX',
			"AREA:thrds_avg#$HalfBlue",
			"AREA:procs_avg#$HalfRed",
			"LINE1:thrds_avg#$FullBlue:Threads  ",
			'GPRINT:thrds_min:MIN:%5.1lf Min,',
			'GPRINT:thrds_avg:AVERAGE:%5.1lf Avg,',
			'GPRINT:thrds_max:MAX:%5.1lf Max,',
			'GPRINT:thrds_avg:LAST:%5.1lf Last\l',
			"LINE1:procs_avg#$FullRed:Processes",
			'GPRINT:procs_min:MIN:%5.1lf Min,',
			'GPRINT:procs_avg:AVERAGE:%5.1lf Avg,',
			'GPRINT:procs_max:MAX:%5.1lf Max,',
			'GPRINT:procs_avg:LAST:%5.1lf Last\l'
		],
		ps_pagefaults => [
			'DEF:minor_avg={file}:minflt:AVERAGE',
			'DEF:minor_min={file}:minflt:MIN',
			'DEF:minor_max={file}:minflt:MAX',
			'DEF:major_avg={file}:majflt:AVERAGE',
			'DEF:major_min={file}:majflt:MIN',
			'DEF:major_max={file}:majflt:MAX',
			'CDEF:minor_major=major_avg,UN,0,major_avg,IF,minor_avg,+',
			"AREA:minor_major#$HalfBlue",
			"AREA:major_avg#$HalfRed",
			"LINE1:minor_major#$FullBlue:Minor",
			'GPRINT:minor_min:MIN:%5.1lf%s Min,',
			'GPRINT:minor_avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:minor_max:MAX:%5.1lf%s Max,',
			'GPRINT:minor_avg:LAST:%5.1lf%s Last\l',
			"LINE1:major_avg#$FullRed:Major",
			'GPRINT:major_min:MIN:%5.1lf%s Min,',
			'GPRINT:major_avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:major_max:MAX:%5.1lf%s Max,',
			'GPRINT:major_avg:LAST:%5.1lf%s Last\l'
		],
		qtype => [
			'DEF:avg={file}:value:AVERAGE',
			'DEF:min={file}:value:MIN',
			'DEF:max={file}:value:MAX',
			"AREA:max#$HalfBlue",
			"AREA:min#$Canvas",
			"LINE1:avg#$FullBlue:Queries/s",
			'GPRINT:min:MIN:%9.3lf Min,',
			'GPRINT:avg:AVERAGE:%9.3lf Average,',
			'GPRINT:max:MAX:%9.3lf Max,',
			'GPRINT:avg:LAST:%9.3lf Last\l'
		],
		swap => [
			'DEF:used_avg={file}:used:AVERAGE',
			'DEF:used_min={file}:used:MIN',
			'DEF:used_max={file}:used:MAX',
			'DEF:free_avg={file}:free:AVERAGE',
			'DEF:free_min={file}:free:MIN',
			'DEF:free_max={file}:free:MAX',
			'DEF:cach_avg={file}:cached:AVERAGE',
			'DEF:cach_min={file}:cached:MIN',
			'DEF:cach_max={file}:cached:MAX',
			'DEF:resv_avg={file}:resv:AVERAGE',
			'DEF:resv_min={file}:resv:MIN',
			'DEF:resv_max={file}:resv:MAX',
			'CDEF:cach_avg_notnull=cach_avg,UN,0,cach_avg,IF',
			'CDEF:resv_avg_notnull=resv_avg,UN,0,resv_avg,IF',
			'CDEF:used_acc=used_avg',
			'CDEF:resv_acc=used_acc,resv_avg_notnull,+',
			'CDEF:cach_acc=resv_acc,cach_avg_notnull,+',
			'CDEF:free_acc=cach_acc,free_avg,+',
			"AREA:free_acc#$HalfGreen",
			"AREA:cach_acc#$HalfBlue",
			"AREA:resv_acc#$HalfYellow",
			"AREA:used_acc#$HalfRed",
			"LINE1:free_acc#$FullGreen:Free    ",
			'GPRINT:free_min:MIN:%5.1lf%s Min,',
			'GPRINT:free_avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:free_max:MAX:%5.1lf%s Max,',
			'GPRINT:free_avg:LAST:%5.1lf%s Last\n',
			"LINE1:cach_acc#$FullBlue:Cached  ",
			'GPRINT:cach_min:MIN:%5.1lf%s Min,',
			'GPRINT:cach_avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:cach_max:MAX:%5.1lf%s Max,',
			'GPRINT:cach_avg:LAST:%5.1lf%s Last\l',
			"LINE1:resv_acc#$FullYellow:Reserved",
			'GPRINT:resv_min:MIN:%5.1lf%s Min,',
			'GPRINT:resv_avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:resv_max:MAX:%5.1lf%s Max,',
			'GPRINT:resv_avg:LAST:%5.1lf%s Last\n',
			"LINE1:used_acc#$FullRed:Used    ",
			'GPRINT:used_min:MIN:%5.1lf%s Min,',
			'GPRINT:used_avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:used_max:MAX:%5.1lf%s Max,',
			'GPRINT:used_avg:LAST:%5.1lf%s Last\l'
		],
		temperature => [
			'DEF:temp_avg={file}:value:AVERAGE',
			'DEF:temp_min={file}:value:MIN',
			'DEF:temp_max={file}:value:MAX',
			"AREA:temp_max#$HalfBlue",
			"AREA:temp_min#$Canvas",
			"LINE1:temp_avg#$FullBlue:Value",
			'GPRINT:temp_min:MIN:%4.1lf Min,',
			'GPRINT:temp_avg:AVERAGE:%4.1lf Avg,',
			'GPRINT:temp_max:MAX:%4.1lf Max,',
			'GPRINT:temp_avg:LAST:%4.1lf Last\l'
		],
		timeleft => [
			'DEF:avg={file}:timeleft:AVERAGE',
			'DEF:min={file}:timeleft:MIN',
			'DEF:max={file}:timeleft:MAX',
			"AREA:max#$HalfBlue",
			"AREA:min#$Canvas",
			"LINE1:avg#$FullBlue:Time left [min]",
			'GPRINT:min:MIN:%5.1lf%s Min,',
			'GPRINT:avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:max:MAX:%5.1lf%s Max,',
			'GPRINT:avg:LAST:%5.1lf%s Last\l'
		],
		time_offset => [ # NTPd
			'DEF:s_avg={file}:seconds:AVERAGE',
			'DEF:s_min={file}:seconds:MIN',
			'DEF:s_max={file}:seconds:MAX',
			"AREA:s_max#$HalfBlue",
			"AREA:s_min#$Canvas",
			"LINE1:s_avg#$FullBlue:{inst}",
			'GPRINT:s_min:MIN:%7.3lf%s Min,',
			'GPRINT:s_avg:AVERAGE:%7.3lf%s Avg,',
			'GPRINT:s_max:MAX:%7.3lf%s Max,',
			'GPRINT:s_avg:LAST:%7.3lf%s Last'
		],
		traffic => ['DEF:out_min_raw={file}:outgoing:MIN',
			'DEF:out_avg_raw={file}:outgoing:AVERAGE',
			'DEF:out_max_raw={file}:outgoing:MAX',
			'DEF:inc_min_raw={file}:incoming:MIN',
			'DEF:inc_avg_raw={file}:incoming:AVERAGE',
			'DEF:inc_max_raw={file}:incoming:MAX',
			'CDEF:out_min=out_min_raw,8,*',
			'CDEF:out_avg=out_avg_raw,8,*',
			'CDEF:out_max=out_max_raw,8,*',
			'CDEF:inc_min=inc_min_raw,8,*',
			'CDEF:inc_avg=inc_avg_raw,8,*',
			'CDEF:inc_max=inc_max_raw,8,*',
			'CDEF:overlap=out_avg,inc_avg,GT,inc_avg,out_avg,IF',
			'CDEF:mytime=out_avg_raw,TIME,TIME,IF',
			'CDEF:sample_len_raw=mytime,PREV(mytime),-',
			'CDEF:sample_len=sample_len_raw,UN,0,sample_len_raw,IF',
			'CDEF:out_avg_sample=out_avg_raw,UN,0,out_avg_raw,IF,sample_len,*',
			'CDEF:out_avg_sum=PREV,UN,0,PREV,IF,out_avg_sample,+',
			'CDEF:inc_avg_sample=inc_avg_raw,UN,0,inc_avg_raw,IF,sample_len,*',
			'CDEF:inc_avg_sum=PREV,UN,0,PREV,IF,inc_avg_sample,+',
			"AREA:out_avg#$HalfGreen",
			"AREA:inc_avg#$HalfBlue",
			"AREA:overlap#$HalfBlueGreen",
			"LINE1:out_avg#$FullGreen:Outgoing",
			'GPRINT:out_avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:out_max:MAX:%5.1lf%s Max,',
			'GPRINT:out_avg:LAST:%5.1lf%s Last',
			'GPRINT:out_avg_sum:LAST:(ca. %5.1lf%sB Total)\l',
			"LINE1:inc_avg#$FullBlue:Incoming",
			#'GPRINT:inc_min:MIN:%5.1lf %s Min,',
			'GPRINT:inc_avg:AVERAGE:%5.1lf%s Avg,',
			'GPRINT:inc_max:MAX:%5.1lf%s Max,',
			'GPRINT:inc_avg:LAST:%5.1lf%s Last',
			'GPRINT:inc_avg_sum:LAST:(ca. %5.1lf%sB Total)\l'
		],
                cpufreq => [
                        'DEF:cpufreq_avg={file}:value:AVERAGE',
                        'DEF:cpufreq_min={file}:value:MIN',
                        'DEF:cpufreq_max={file}:value:MAX',
                        "AREA:cpufreq_max#$HalfBlue",
                        "AREA:cpufreq_min#$Canvas",
                        "LINE1:cpufreq_avg#$FullBlue:Frequency",
                        'GPRINT:cpufreq_min:MIN:%5.1lf%s Min,',
                        'GPRINT:cpufreq_avg:AVERAGE:%5.1lf%s Avg,',
                        'GPRINT:cpufreq_max:MAX:%5.1lf%s Max,',
                        'GPRINT:cpufreq_avg:LAST:%5.1lf%s Last\l'
                ],
		multimeter => [
			    'DEF:multimeter_avg={file}:value:AVERAGE',
			    'DEF:multimeter_min={file}:value:MIN',
			    'DEF:multimeter_max={file}:value:MAX',
			    "AREA:multimeter_max#$HalfBlue",
			    "AREA:multimeter_min#$Canvas",
			    "LINE1:multimeter_avg#$FullBlue:Multimeter",
			    'GPRINT:multimeter_min:MIN:%4.1lf Min,',
			    'GPRINT:multimeter_avg:AVERAGE:%4.1lf Average,',
			    'GPRINT:multimeter_max:MAX:%4.1lf Max,',
			    'GPRINT:multimeter_avg:LAST:%4.1lf Last\l'
		],
		users => [
			    'DEF:users_avg={file}:users:AVERAGE',
			    'DEF:users_min={file}:users:MIN',
			    'DEF:users_max={file}:users:MAX',
			    "AREA:users_max#$HalfBlue",
			    "AREA:users_min#$Canvas",
			    "LINE1:users_avg#$FullBlue:Users",
			    'GPRINT:users_min:MIN:%4.1lf Min,',
			    'GPRINT:users_avg:AVERAGE:%4.1lf Average,',
			    'GPRINT:users_max:MAX:%4.1lf Max,',
			    'GPRINT:users_avg:LAST:%4.1lf Last\l'
		],
		voltage => [
			'DEF:avg={file}:voltage:AVERAGE',
			'DEF:min={file}:voltage:MIN',
			'DEF:max={file}:voltage:MAX',
			"AREA:max#$HalfBlue",
			"AREA:min#$Canvas",
			"LINE1:avg#$FullBlue:Voltage",
			'GPRINT:min:MIN:%5.1lf%sV Min,',
			'GPRINT:avg:AVERAGE:%5.1lf%sV Avg,',
			'GPRINT:max:MAX:%5.1lf%sV Max,',
			'GPRINT:avg:LAST:%5.1lf%sV Last\l'
		],
		vs_threads => [
			"DEF:total_avg={file}:total:AVERAGE",
			"DEF:total_min={file}:total:MIN",
			"DEF:total_max={file}:total:MAX",
			"DEF:running_avg={file}:running:AVERAGE",
			"DEF:running_min={file}:running:MIN",
			"DEF:running_max={file}:running:MAX",
			"DEF:uninterruptible_avg={file}:uninterruptible:AVERAGE",
			"DEF:uninterruptible_min={file}:uninterruptible:MIN",
			"DEF:uninterruptible_max={file}:uninterruptible:MAX",
			"DEF:onhold_avg={file}:onhold:AVERAGE",
			"DEF:onhold_min={file}:onhold:MIN",
			"DEF:onhold_max={file}:onhold:MAX",
			"LINE1:total_avg#$FullYellow:Total   ",
			'GPRINT:total_min:MIN:%5.1lf Min,',
			'GPRINT:total_avg:AVERAGE:%5.1lf Avg.,',
			'GPRINT:total_max:MAX:%5.1lf Max,',
			'GPRINT:total_avg:LAST:%5.1lf Last\l',
			"LINE1:running_avg#$FullRed:Running ",
			'GPRINT:running_min:MIN:%5.1lf Min,',
			'GPRINT:running_avg:AVERAGE:%5.1lf Avg.,',          
			'GPRINT:running_max:MAX:%5.1lf Max,',
			'GPRINT:running_avg:LAST:%5.1lf Last\l',
			"LINE1:uninterruptible_avg#$FullGreen:Unintr  ",
			'GPRINT:uninterruptible_min:MIN:%5.1lf Min,',
			'GPRINT:uninterruptible_avg:AVERAGE:%5.1lf Avg.,',
			'GPRINT:uninterruptible_max:MAX:%5.1lf Max,',
			'GPRINT:uninterruptible_avg:LAST:%5.1lf Last\l',
			"LINE1:onhold_avg#$FullBlue:Onhold  ",
			'GPRINT:onhold_min:MIN:%5.1lf Min,',
			'GPRINT:onhold_avg:AVERAGE:%5.1lf Avg.,',
			'GPRINT:onhold_max:MAX:%5.1lf Max,',
			'GPRINT:onhold_avg:LAST:%5.1lf Last\l'
		],
		vs_memory => [
			'DEF:vm_avg={file}:vm:AVERAGE',
			'DEF:vm_min={file}:vm:MIN',
			'DEF:vm_max={file}:vm:MAX',
			'DEF:vml_avg={file}:vml:AVERAGE',
			'DEF:vml_min={file}:vml:MIN',
			'DEF:vml_max={file}:vml:MAX',
			'DEF:rss_avg={file}:rss:AVERAGE',
			'DEF:rss_min={file}:rss:MIN',
			'DEF:rss_max={file}:rss:MAX',
			'DEF:anon_avg={file}:anon:AVERAGE',
			'DEF:anon_min={file}:anon:MIN',
			'DEF:anon_max={file}:anon:MAX',
			"LINE1:vm_avg#$FullYellow:VM     ",
			'GPRINT:vm_min:MIN:%5.1lf%s Min,',
			'GPRINT:vm_avg:AVERAGE:%5.1lf%s Avg.,',
			'GPRINT:vm_max:MAX:%5.1lf%s Avg.,',
			'GPRINT:vm_avg:LAST:%5.1lf%s Last\l',
			"LINE1:vml_avg#$FullRed:Locked ",
			'GPRINT:vml_min:MIN:%5.1lf%s Min,',
			'GPRINT:vml_avg:AVERAGE:%5.1lf%s Avg.,',
			'GPRINT:vml_max:MAX:%5.1lf%s Avg.,',
			'GPRINT:vml_avg:LAST:%5.1lf%s Last\l',
			"LINE1:rss_avg#$FullGreen:RSS    ",
			'GPRINT:rss_min:MIN:%5.1lf%s Min,',
			'GPRINT:rss_avg:AVERAGE:%5.1lf%s Avg.,',
			'GPRINT:rss_max:MAX:%5.1lf%s Avg.,',
			'GPRINT:rss_avg:LAST:%5.1lf%s Last\l',
			"LINE1:anon_avg#$FullBlue:Anon.  ",
			'GPRINT:anon_min:MIN:%5.1lf%s Min,',
			'GPRINT:anon_avg:AVERAGE:%5.1lf%s Avg.,',
			'GPRINT:anon_max:MAX:%5.1lf%s Avg.,',
			'GPRINT:anon_avg:LAST:%5.1lf%s Last\l',
		],
		vs_processes => [
			'DEF:proc_avg={file}:total:AVERAGE',
			'DEF:proc_min={file}:total:MIN',
			'DEF:proc_max={file}:total:MAX',
			"AREA:proc_max#$HalfBlue",
			"AREA:proc_min#$Canvas",
			"LINE1:proc_avg#$FullBlue:Processes",
			'GPRINT:proc_min:MIN:%4.1lf Min,',
			'GPRINT:proc_avg:AVERAGE:%4.1lf Avg.,',
			'GPRINT:proc_max:MAX:%4.1lf Max,',
			'GPRINT:proc_avg:LAST:%4.1lf Last\l'
		],
	};
	$GraphDefs->{'disk'} = $GraphDefs->{'partition'};
	$GraphDefs->{'if_errors'} = $GraphDefs->{'if_packets'};
	$GraphDefs->{'meminfo'} = $GraphDefs->{'memory'};
	$GraphDefs->{'sensors'} = $GraphDefs->{'temperature'};

	$GraphDefs->{'delay'}           = $GraphDefs->{'time_offset'};
	$GraphDefs->{'time_dispersion'} = $GraphDefs->{'time_offset'};
}

our $GraphArgs =
{
	apache_bytes => ['-t', 'apache traffic', '-v', 'Bit/s'],
	apache_requests => ['-t', 'apache requests', '-v', 'Requests/s'],
	apache_scoreboard => ['-t', 'apache scoreboard {inst}', '-v', 'Processes'],
	charge => ['-t', '{host} charge', '-v', 'Ampere hours'],
	charge_percent => ['-t', '{host} charge', '-v', 'Percent'],
	cpu => ['-t', '{host} cpu{inst} usage', '-v', 'Percent', '-l', '0'],
	cpufreq => ['-t', '{host} cpu{inst} usage', '-v', 'Mhz'],
	current => ['-t', '{host} current', '-v', 'Ampere'],
	#disk => ['-t', '{host} disk {inst} IO wait', '-v', 'Seconds'],
	delay => ['-t', 'NTPd peer delay ({inst})', '-v', 'Seconds'],
	df => ['-t', '{host}:{inst} usage', '-v', 'Percent', '-l', '0'],
	disk => ['-t', '{host} disk {inst} usage', '-v', 'Byte/s'],
	fanspeed => ['-t', '{host} fanspeed {inst}', '-v', 'rpm'],
	frequency_offset => ['-t', 'NTPd frequency offset ({inst})', '-v', 'Parts per million'],
	hddtemp => ['-t', '{host} hdd temperature {inst}', '-v', '°Celsius'],
	if_errors => ['-t', '{host} {inst} errors', '-v', 'Errors/s'],
	if_packets => ['-t', '{host} {inst} packets', '-v', 'Packets/s'],
	load => ['-t', '{host} load average', '-v', 'System load', '-X', '0'],
	load_percent => ['-t', '{host} load', '-v', 'Percent'],
	mails   => ['-t', '{host} mail count', '-v', 'Amount', '-X', '0'],
	memory => ['-t', '{host} memory usage', '-v', 'Bytes', '-b', '1024', '-l', '0'],
	mysql_commands => ['-t', 'mysql command {inst}', '-v', 'Issues/s' ],
	mysql_handler => ['-t', 'mysql handler {inst}', '-v', 'Issues/s' ],
	mysql_qcache => ['-t', 'mysql query cache', '-v', 'Queries/s' ],
	mysql_threads => ['-t', 'mysql threads', '-v', 'Threads' ],
	nfs3_procedures => ['-t', '{host} NFSv3 {inst} procedures', '-v', 'Procedures/s' ],
	partition => ['-t', '{host} partition {inst} usage', '-v', 'Byte/s'],
	ping => ['-t', '{host} ping to {inst}', '-v', 'ms'],
	processes => ['-t', '{host} processes', '-v', 'Processes'],
	ps_rss => ['-t', '{host} process {inst} RSS', '-v', 'Bytes', '-b', '1024'],
	ps_cputime => ['-t', '{host} process {inst} CPU usage', '-v', 'Seconds'],
	ps_count => ['-t', '{host} process {inst} count', '-v', 'Threads/Processes'],
	ps_pagefaults => ['-t', '{host} process {inst} pagefaults', '-v', 'Pagefaults/s'],
	qtype => ['-t', 'QType {inst}', '-v', 'Queries/s'],
	sensors => ['-t', '{host} sensor {inst}', '-v', '°Celsius'],
	swap => ['-t', '{host} swap usage', '-v', 'Bytes', '-b', '1024', '-l', '0'],
	temperature => ['-t', '{host} temperature {inst}', '-v', '°Celsius'],
	timeleft => ['-t', '{host} UPS time left', '-v', 'Time [min]'],
	time_offset => ['-t', 'NTPd time offset ({inst})', '-v', 'Seconds'],
	time_dispersion => ['-t', 'NTPd time dispersion ({inst})', '-v', 'Seconds'],
	traffic => ['-t', '{host} {inst} traffic', '-v', 'Bit/s'],
	users => ['-t', '{host} users', '-v', 'Users'],
	multimeter => ['-t', '{host} multimeter', '-v', 'Value'],
	voltage => ['-t', '{host} voltage', '-v', 'Volts'],
	vs_threads => ['-t', '{host} threads', '-v', 'Threads'],
	vs_memory => ['-t', '{host} memory usage', '-v', 'Bytes'],
	vs_processes => ['-t', '{host} processes', '-v', 'Processes'],
};

our $GraphMulti =
{
	apache_scoreboard => \&output_graph_apache_scoreboard,
	cpu	=> \&output_graph_cpu,
	cpufreq => 1,
	disk	=> 1,
	load	=> 0,
	mails	=> 0,
	memory	=> 0,
	mysql_commands => \&output_graph_mysql_commands,
	mysql_handler => \&output_graph_mysql_handler,
	partition => 1,
	ping	=> \&output_graph_ping,
	qtype => \&output_graph_named_qtype,
	sensors	=> 1,
	traffic	=> 1,
	users => 1,
	multimeter => 1
};

our @Info;
if (defined ($ENV{'GATEWAY_INTERFACE'}))
{
	@Info = ($ENV{'PATH_INFO'} || '') =~ m#([\w\-\.]+)#g;
}
else
{
	@Info = @ARGV;
}

parse_pathinfo (@Info);

if ($TimeSpan)
{
	output_graph ();
}
else
{
	output_page ();
}

exit (0);

sub output_graph_cpu
{
	my @inst = @_;
	my @ret = ();

	die if (@inst < 2);

	for (@inst)
	{
		push (@ret,
			"DEF:user_avg_$_=$AbsDir/cpu-$_.rrd:user:AVERAGE",
			"DEF:user_min_$_=$AbsDir/cpu-$_.rrd:user:MIN",
			"DEF:user_max_$_=$AbsDir/cpu-$_.rrd:user:MAX",
			"DEF:nice_avg_$_=$AbsDir/cpu-$_.rrd:nice:AVERAGE",
			"DEF:nice_min_$_=$AbsDir/cpu-$_.rrd:nice:MIN",
			"DEF:nice_max_$_=$AbsDir/cpu-$_.rrd:nice:MAX",
			"DEF:syst_avg_$_=$AbsDir/cpu-$_.rrd:syst:AVERAGE",
			"DEF:syst_min_$_=$AbsDir/cpu-$_.rrd:syst:MIN",
			"DEF:syst_max_$_=$AbsDir/cpu-$_.rrd:syst:MAX",
			"DEF:wait_avg_$_=$AbsDir/cpu-$_.rrd:wait:AVERAGE",
			"DEF:wait_min_$_=$AbsDir/cpu-$_.rrd:wait:MIN",
			"DEF:wait_max_$_=$AbsDir/cpu-$_.rrd:wait:MAX");
	}

	for (qw(user nice syst wait))
	{
		my $def = $_;
		my $cdef;

		my $default_value = ($def eq 'user' or $def eq 'syst') ? 'UNKN' : '0';

		for (qw(avg min max))
		{
			my $cf = $_;

			for (@inst)
			{
				push (@ret, "CDEF:${def}_${cf}_notnull_${_}=${def}_${cf}_${_},UN,0,${def}_${cf}_${_},IF");
				push (@ret, "CDEF:${def}_${cf}_defined_${_}=${def}_${cf}_${_},UN,0,1,IF");
			}

			$cdef = "CDEF:${def}_${cf}_num=" . join (',', map { "${def}_${cf}_defined_${_}" } (@inst));
			$cdef .= ',+' x (scalar (@inst) - 1);
			push (@ret, $cdef);

			$cdef = "CDEF:${def}_${cf}=${def}_${cf}_num," . join (',', map { "${def}_${cf}_notnull_${_}" } (@inst));
			$cdef .= ',+' x (scalar (@inst) - 1);
			$cdef .= ",${def}_${cf}_num,${def}_${cf}_num,1,IF,/,$default_value,IF";
			push (@ret, $cdef);
			push (@ret, "CDEF:${def}_${cf}_notnull=${def}_${cf},UN,0,${def}_${cf},IF");
		}
	}

	push (@ret,
		"CDEF:nice_acc=syst_avg_notnull,wait_avg_notnull,user_avg_notnull,nice_avg_notnull,+,+,+",
		"CDEF:user_acc=syst_avg_notnull,wait_avg_notnull,user_avg_notnull,+,+",
		"CDEF:wait_acc=syst_avg_notnull,wait_avg_notnull,+",
		"CDEF:syst_acc=syst_avg_notnull");

	push (@ret, grep { $_ !~ m/^C?DEF/ } (@{$GraphDefs->{'cpu'}}));

	return (@ret);
}

sub output_graph_apache_scoreboard
{
	my @inst = @_;
	my @ret = ();

	die if (@inst < 2);

	my @colors = get_n_colors (scalar (@inst));

	for (my $i = 0; $i < scalar (@inst); $i++)
	{
		my $inst = $inst[$i];
		push (@ret,
			"DEF:avg_$i=$AbsDir/apache_scoreboard-$inst.rrd:count:AVERAGE",
			"DEF:min_$i=$AbsDir/apache_scoreboard-$inst.rrd:count:MIN",
			"DEF:max_$i=$AbsDir/apache_scoreboard-$inst.rrd:count:MAX");
	}

	for (my $i = 0; $i < scalar (@inst); $i++)
	{
		my $inst = $inst[$i];
		my $color = $colors[$i];

		if (length ($inst) > 15)
		{
			$inst = substr ($inst, 0, 12) . '...';
		}
		else
		{
			$inst = sprintf ('%-15s', $inst);
		}

		push (@ret,
			"LINE1:avg_$i#$color:$inst",
			"GPRINT:min_$i:MIN:%6.2lf Min,",
			"GPRINT:avg_$i:AVERAGE:%6.2lf Avg,",
			"GPRINT:max_$i:MAX:%6.2lf Max,",
			"GPRINT:avg_$i:LAST:%6.2lf Last\\l");
	}

	return (@ret);
}

sub output_graph_ping
{
	my @inst = @_;
	my @ret = ();

	die if (@inst < 2);

	my @colors = get_n_colors (scalar (@inst));

	for (my $i = 0; $i < scalar (@inst); $i++)
	{
		my $inst = $inst[$i];
		push (@ret,
			"DEF:avg_$i=$AbsDir/ping-$inst.rrd:ping:AVERAGE",
			"DEF:min_$i=$AbsDir/ping-$inst.rrd:ping:MIN",
			"DEF:max_$i=$AbsDir/ping-$inst.rrd:ping:MAX");
	}

	for (my $i = 0; $i < scalar (@inst); $i++)
	{
		my $inst = $inst[$i];
		my $color = $colors[$i];

		if (length ($inst) > 15)
		{
			$inst = substr ($inst, 0, 12) . '...';
		}
		else
		{
			$inst = sprintf ('%-15s', $inst);
		}

		push (@ret,
			"LINE1:avg_$i#$color:$inst",
			"GPRINT:min_$i:MIN:%4.1lf ms Min,",
			"GPRINT:avg_$i:AVERAGE:%4.1lf ms Avg,",
			"GPRINT:max_$i:MAX:%4.1lf ms Max,",
			"GPRINT:avg_$i:LAST:%4.1lf ms Last\\l");
	}

	return (@ret);
}

sub output_graph_mysql_commands
{
	my @inst = @_;
	my @ret = ();

	die if (@inst < 2);

	my @colors = get_n_colors (scalar (@inst));

	for (my $i = 0; $i < scalar (@inst); $i++)
	{
		my $inst = $inst[$i];
		push (@ret,
			"DEF:avg_$i=$AbsDir/mysql_commands-$inst.rrd:value:AVERAGE",
			"DEF:min_$i=$AbsDir/mysql_commands-$inst.rrd:value:MIN",
			"DEF:max_$i=$AbsDir/mysql_commands-$inst.rrd:value:MAX");
	}

	for (my $i = 0; $i < scalar (@inst); $i++)
	{
		my $inst = $inst[$i];
		my $color = $colors[$i];

		if (length ($inst) > 18)
		{
			$inst = substr ($inst, 0, 15) . '...';
		}
		else
		{
			$inst = sprintf ('%-18s', $inst);
		}

		push (@ret,
			"LINE1:avg_$i#$color:$inst",
			"GPRINT:min_$i:MIN:%6.1lf Min,",
			"GPRINT:avg_$i:AVERAGE:%6.1lf Avg,",
			"GPRINT:max_$i:MAX:%6.1lf Max,",
			"GPRINT:avg_$i:LAST:%6.1lf Last\\l");
	}

	return (@ret);
}

sub output_graph_mysql_handler
{
	my @inst = @_;
	my @ret = ();

	die if (@inst < 2);

	my @colors = get_n_colors (scalar (@inst));

	for (my $i = 0; $i < scalar (@inst); $i++)
	{
		my $inst = $inst[$i];
		push (@ret,
			"DEF:avg_$i=$AbsDir/mysql_handler-$inst.rrd:value:AVERAGE",
			"DEF:min_$i=$AbsDir/mysql_handler-$inst.rrd:value:MIN",
			"DEF:max_$i=$AbsDir/mysql_handler-$inst.rrd:value:MAX");
	}

	for (my $i = 0; $i < scalar (@inst); $i++)
	{
		my $inst = $inst[$i];
		my $color = $colors[$i];

		if (length ($inst) > 18)
		{
			$inst = substr ($inst, 0, 15) . '...';
		}
		else
		{
			$inst = sprintf ('%-18s', $inst);
		}

		push (@ret,
			"LINE1:avg_$i#$color:$inst",
			"GPRINT:min_$i:MIN:%6.1lf Min,",
			"GPRINT:avg_$i:AVERAGE:%6.1lf Avg,",
			"GPRINT:max_$i:MAX:%6.1lf Max,",
			"GPRINT:avg_$i:LAST:%6.1lf Last\\l");
	}

	return (@ret);
}

sub output_graph_named_qtype
{
	my @inst = @_;
	my @ret = ();

	die if (@inst < 2);

	my @colors = get_n_colors (scalar (@inst));

	for (my $i = 0; $i < scalar (@inst); $i++)
	{
		my $inst = $inst[$i];
		push (@ret,
			"DEF:avg_$i=$AbsDir/qtype-$inst.rrd:value:AVERAGE",
			"DEF:min_$i=$AbsDir/qtype-$inst.rrd:value:MIN",
			"DEF:max_$i=$AbsDir/qtype-$inst.rrd:value:MAX");
	}

	for (my $i = 0; $i < scalar (@inst); $i++)
	{
		my $inst = $inst[$i];
		my $color = $colors[$i];
		my $type = ($i == 0) ? 'AREA' : 'STACK';

		if (length ($inst) > 5)
		{
			$inst = substr ($inst, 0, 5);
		}
		else
		{
			$inst = sprintf ('%-5s', $inst);
		}

		push (@ret,
			"$type:avg_$i#$color:$inst",
			"GPRINT:min_$i:MIN:%9.3lf Min,",
			"GPRINT:avg_$i:AVERAGE:%9.3lf Avg,",
			"GPRINT:max_$i:MAX:%9.3lf Max,",
			"GPRINT:avg_$i:LAST:%9.3lf Last\\l");
	}

	return (@ret);
}
sub output_graph
{
	die unless (defined ($GraphDefs->{$Type}));

	my $host;
	my @cmd = ();
	my $file = $AbsDir . '/';
	my $files = get_all_files ($AbsDir);

	#
	# get hostname
	#
	if ($RelDir =~ m#([^/]+)$#)
	{
		$host = $1;
	}
	else
	{
		$host = $Config->{'HostName'};
	}

	#
	# get timespan
	#
	if ($TimeSpan =~ m/(\d+)/)
	{
		$TimeSpan = -1 * int ($1);
	}
	else
	{
		my %t = (hour => -3600, day => -86400, week => -604800, month => -2678400, year => -31622400);
		die unless (defined ($t{$TimeSpan}));
		$TimeSpan = $t{$TimeSpan};
	}

	if (scalar (@{$files->{$Type}}) == 1)
	{
		$Inst = $files->{$Type}[0];
	}

	#push (@cmd, '-', '-a', 'PNG', '-s', $TimeSpan, '-w', 800, '-h', 150);
	push (@cmd, '-', '-a', 'PNG', '-s', $TimeSpan);
	push (@cmd, @{$GraphArgs->{$Type}}) if (defined ($GraphArgs->{$Type}));

	for (qw(Back ShadeA ShadeB Font Canvas Grid MGrid Frame Arrow))
	{
		push (@cmd, '-c', uc ($_) . '#' . $Config->{'Colors'}{$_});
	}

	if ((length ($Inst) == 0) and (ref ($GraphMulti->{$Type}) eq 'CODE'))
	{
		push (@cmd, $GraphMulti->{$Type}->(@{$files->{$Type}}));
	}
	else
	{
		if (length ("$Inst"))
		{
			$file .= "$Type-$Inst.rrd";
		}
		else
		{
			$file .= "$Type.rrd";
		}

		die ("File not found: $file") unless (-e $file);

		push (@cmd, @{$GraphDefs->{$Type}});
	}

	for (@cmd)
	{
		$_ =~ s/{file}/$file/g;
		$_ =~ s/{host}/$host/g;
		$_ =~ s/{inst}/$Inst/g;
		$_ =~ s/{type}/$Type/g;
	}

	$| = 1;

	print STDOUT <<HEADER if (defined ($ENV{'GATEWAY_INTERFACE'}));
Content-Type: image/png
Cache-Control: no-cache

HEADER

	if (1)
	{
		my $fh;
		open ($fh, ">/tmp/collection.log") or die ("open: $!");
		flock ($fh, LOCK_EX) or die ("flock: $!");

		print $fh join ("\n\t", @cmd) . "\n";

		close ($fh);
	}

	RRDs::graph (@cmd);

	die ('RRDs::error: ' . RRDs::error ()) if (RRDs::error ());
}

sub output_page
{
	my $files = get_all_files ($AbsDir);
	my $dirs  = get_all_dirs  ($AbsDir);

	print STDOUT <<HEADER if (defined ($ENV{'GATEWAY_INTERFACE'}));
Content-Type: text/html
Cache-Control: no-cache

<html>
	<head>
		<title>Collection: $RelDir</title>
		<style type="text/css">
			img { border: none; display: block; }
		</style>
	</head>

	<body>
HEADER

	my $MySelf = defined ($ENV{'GATEWAY_INTERFACE'}) ? $ENV{'SCRIPT_NAME'} : $0;

	if ((length ($Type) != 0) and (length ($Inst) == 0) and (ref ($GraphMulti->{$Type}) eq 'CODE') and (scalar (@{$files->{$Type}}) > 1))
	{
		print qq(\t\t<div><a href="$MySelf$RelDir">Go up</a></div>\n);

		print "\t\t<ul>\n";
		for (@{$files->{$Type}})
		{
			print qq(\t\t\t<li><a href="$MySelf$RelDir/$Type/$_">$_</a></li>\n);
		}
		print <<HTML;
		</ul>

		<h3>Hourly</h3>
		<div><img src="$MySelf$RelDir/$Type/hour" /></div>
		<h3>Daily</h3>
		<div><img src="$MySelf$RelDir/$Type/day" /></div>
		<h3>Weekly</h3>
		<div><img src="$MySelf$RelDir/$Type/week" /></div>
		<h3>Monthly</h3>
		<div><img src="$MySelf$RelDir/$Type/month" /></div>
		<h3>Yearly</h3>
		<div><img src="$MySelf$RelDir/$Type/year" /></div>
HTML
	}
	elsif (length ($Type) != 0)
	{
		my $ext = length ($Inst) ? "$Type/$Inst" : $Type;

		if ((ref ($GraphMulti->{$Type}) eq 'CODE') and (scalar (@{$files->{$Type}}) > 1))
		{
			print qq(<div><a href="$MySelf$RelDir/$Type">Go up</a></div>\n);
		}
		else
		{
			print qq(<div><a href="$MySelf$RelDir">Go up</a></div>\n);
		}

		print <<HTML;
		<h3>Hourly</h3>
		<div><img src="$MySelf$RelDir/$ext/hour" /></div>
		<h3>Daily</h3>
		<div><img src="$MySelf$RelDir/$ext/day" /></div>
		<h3>Weekly</h3>
		<div><img src="$MySelf$RelDir/$ext/week" /></div>
		<h3>Monthly</h3>
		<div><img src="$MySelf$RelDir/$ext/month" /></div>
		<h3>Yearly</h3>
		<div><img src="$MySelf$RelDir/$ext/year" /></div>
HTML
	}
	else
	{
		if ($RelDir)
		{
			my ($up) = $RelDir =~ m#(.*)/[^/]+$#;
			print qq(\t\t<div><a href="$MySelf$up">Go up</a></div>\n);
		}

		if (@$dirs)
		{
			print "<ul>\n";
			for (@$dirs)
			{
				print qq(<li>$AbsDir/<a href="$MySelf$RelDir/$_">$_</a></li>\n);
			}
			print "</ul>\n";
		}

		for (sort (keys %$files))
		{
			my $type = $_;

			if (ref ($GraphMulti->{$type}) eq 'CODE')
			{
				print qq(\t\t<a href="$MySelf$RelDir/$type" />),
				qq(<img src="$MySelf$RelDir/$type/day" /></a>\n);
				next;
			}

			for (@{$files->{$type}})
			{
				my $inst = "$_";

				if (length ($inst))
				{
					print qq(\t\t<a href="$MySelf$RelDir/$type/$inst" />),
					qq(<img src="$MySelf$RelDir/$type/$inst/day" /></a>\n);
				}
				else
				{
					print qq(\t\t<a href="$MySelf$RelDir/$type" />),
					qq(<img src="$MySelf$RelDir/$type/day" /></a>\n);
				}
			}
		}
	}

	print STDOUT <<FOOTER if (defined ($ENV{'GATEWAY_INTERFACE'}));
	</body>
</html>
FOOTER
}

sub output_xml
{
	my $files = get_all_files ();

	print STDOUT <<HEADER if (defined ($ENV{'GATEWAY_INTERFACE'}));
Content-Type: text/xml
Cache-Control: no-cache

HEADER
	print STDOUT pl2xml ($files);
}

sub read_config
{
	my $file = @_ ? shift : '/etc/collection.conf';
	my $conf;
	my $fh;

#	if (open ($fh, "< $file"))
#	{
#		my $xml;
#		local $/ = undef;
#		$xml = <$fh>;
#
#		eval
#		{
#			$conf = xml2pl ($xml);
#		};
#		close ($fh);
#	}

	if (!$conf)
	{
		return ({
				Colors =>
				{
					Back	=> 'FFFFFF',
					ShadeA	=> 'FFFFFF',
					ShadeB	=> 'FFFFFF',
					Font	=> '000000',
					Canvas	=> 'F5F5F5',
					Grid	=> 'D0D0D0',
					MGrid	=> 'A0A0A0',
					Frame	=> '646464',
					Arrow	=> 'FF0000',

					FullRed		=> 'FF0000',
					FullBlue	=> '0000FF',
					FullGreen	=> '00E000',
					FullYellow	=> 'F0A000',
					FullCyan	=> '00A0FF',
					FullMagenta	=> 'A000FF',
					Alpha		=> 0.25,
					HalfRed		=> 'F8B8B8',
					HalfBlue	=> 'B8B8F8',
					HalfGreen	=> 'B8F0B8',
					HalfYellow	=> 'F4F4B8'
				},
				Directory => '/var/lib/collectd',
				HostName  => (defined ($ENV{'SERVER_NAME'}) ? $ENV{'SERVER_NAME'} : 'localhost')
			});
	}
	else
	{
		return ($conf);
	}
}

sub parse_pathinfo
{
	my @info = @_;

	$AbsDir = $Config->{'Directory'};
	$RelDir = '';

	while (@info and -d $AbsDir . '/' . $info[0])
	{
		my $new = shift (@info);
		next if ($new =~ m/^\./);

		$AbsDir .= '/' . $new;
		$RelDir .= '/' . $new;
	}

	$Type = '';
	$Inst = '';
	$TimeSpan = '';

	confess ("parse_pathinfo: too many elements in pathinfo") if (scalar (@info) > 3);
	return unless (@info);

	$Type = shift (@info);
	return unless (@info);

	if ($info[-1] =~ m/^(hour|day|week|month|year)$/i)
	{
		$TimeSpan = pop (@info);
	}

	$Inst = shift (@info) if (@info);

	confess ("unrecognized elements in pathinfo") if (@info);
}

sub get_all_files
{
	my $dir = @_ ? shift : $Config->{'Directory'};
	my $hash = {};
	my $dh;

	if (opendir ($dh, $dir))
	{
		while (my $thing = readdir ($dh))
		{
			next if ($thing =~ m/^\./);

			my $type;
			my $inst;

			if ($thing =~ m/^(\w+)-([\w\-\.]+)\.rrd$/)
			{
				$type = $1;
				$inst = $2;
			}
			elsif ($thing =~ m/^(\w+)\.rrd$/)
			{
				$type = $1;
				$inst = '';
			}
			else
			{
				next;
			}

			# Only load RRD files we can actually display..
			next unless (defined ($GraphDefs->{$type}));

			$hash->{$type} = [] unless (defined ($hash->{$type}));
			push (@{$hash->{$type}}, $inst);
		}

		closedir ($dh);
	}

	return ($hash);
}

sub get_all_dirs
{
	my $dir = @_ ? shift : $Config->{'Directory'};
	my @ret = ();
	my $dh;

	if (opendir ($dh, $dir))
	{
		while (my $thing = readdir ($dh))
		{
			next if ($thing =~ m/^\./);

			next if (!-d "$dir/$thing");

			push (@ret, $thing);
		}

		closedir ($dh);
	}

	return (@ret) if (wantarray ());
	return (\@ret);
}

sub color_hex2rgb
{
	my $color = shift;

	my ($red, $green, $blue) = map { ord (pack ("H2", $_)) } ($color =~ m/([A-Fa-f0-9]{2})/g);
	#print STDERR "$color -> rgb($red,$green,$blue)\n";

	return ($red, $green, $blue);
}

sub color_rgb2hex
{
	croak unless (scalar (@_) == 3);
	
	my ($red, $green, $blue) = @_;

	my $ret = sprintf ("%02X%02X%02X", $red, $green, $blue);
	#print STDERR "rgb($red,$green,$blue) -> $ret\n";

	return ($ret);
}

sub color_calculate_transparent
{
	my $alpha = shift;
	my $canvas = [color_hex2rgb (shift)];
	my @colors = map { [color_hex2rgb ($_)] } (@_);

	if (($alpha < 0.0) or ($alpha > 1.0))
	{
		$alpha = 1.0;
	}

	if ($alpha == 0.0)
	{
		return (color_rgb2hex (@$canvas));
	}
	if ($alpha == 1.0)
	{
		return (color_rgb2hex (@{$colors[-1]}));
	}

	my $ret = _color_calculate_transparent ($alpha, $canvas, @colors);

	return (color_rgb2hex (@$ret));
}

sub _color_calculate_transparent
{
	my $alpha = shift;
	my $canvas = shift;
	my $color = shift;
	my @colors = @_ ? shift : ();
	my $ret = [0, 0, 0];

	for (my $i = 0; $i < 3; $i++)
	{
		$ret->[$i] = ($alpha * $color->[$i]) + ((1 - $alpha) * $canvas->[$i]);
	}

	return (_color_calculate_transparent ($alpha, $ret, @colors)) if (@colors);
	return ($ret);
}

sub get_n_colors
{
	my $num = shift;
	my @ret = ();

	for (my $i = 0; $i < $num; $i++)
	{
		my $pos = 6 * $i / $num;
		my $n = int ($pos);
		my $p = $pos - $n;
		my $q = 1 - $p;

		my $red   = 0;
		my $green = 0;
		my $blue  = 0;

		if ($n == 0)
		{
			$red  = 255;
			$blue = 255 * $p;
		}
		elsif ($n == 1)
		{
			$red  = 255 * $q;
			$blue = 255;
		}
		elsif ($n == 2)
		{
			$green = 255 * $p;
			$blue  = 255;
		}
		elsif ($n == 3)
		{
			$green = 255;
			$blue  = 255 * $q;
		}
		elsif ($n == 4)
		{
			$red   = 255 * $p;
			$green = 255;
		}
		elsif ($n == 5)
		{
			$red   = 255;
			$green = 255 * $q;
		}
		else { die; }

		push (@ret, sprintf ("%02x%02x%02x", $red, $green, $blue));
	}

	return (@ret);
}
