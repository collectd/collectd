#!/usr/bin/perl

use strict;
use warnings;

use Carp (qw(cluck confess));
use CGI (':cgi');
use CGI::Carp ('fatalsToBrowser');
use HTML::Entities ('encode_entities');
use URI::Escape ('uri_escape');
use RRDs ();
use Data::Dumper ();

our $Config = "/etc/collection.conf";
our @DataDirs = ();
our $LibDir;

our @RRDDefaultArgs = ('-w', '800');

our $Args = {};

our $GraphDefs;
load_graph_definitions ();

for (qw(action host plugin plugin_instance type type_instance timespan))
{
	$Args->{$_} = param ($_);
}

exit (main ());

sub read_config
{
	my $fh;
	open ($fh, "< $Config") or confess ("open ($Config): $!");
	while (my $line = <$fh>)
	{
		chomp ($line);
		next if (!$line);
		next if ($line =~ m/^\s*#/);
		next if ($line =~ m/^\s*$/);

		my $key;
		my $value;

		if ($line =~ m/^([A-Za-z]+):\s*"((?:[^"\\]+|\\.)*)"$/)
		{
			$key = lc ($1); $value = $2;
			$value =~ s/\\(.)/$1/g;
		}
		elsif ($line =~ m/([A-Za-z]+):\s*([0-9]+)$/)
		{
			$key = lc ($1); $value = 0 + $2;
		}
		else
		{
			print STDERR "Cannot parse line: $line\n";
			next;
		}

		if ($key eq 'datadir')
		{
			$value =~ s#/*$##;
			push (@DataDirs, $value);
		}
		elsif ($key eq 'libdir')
		{
			$value =~ s#/*$##;
			$LibDir = $value;
		}
		else
		{
			print STDERR "Unknown key: $key\n";
		}
	}
	close ($fh);
} # read_config

#sub validate_args
#{
#	if (!$Args->{'host'} || ($Args->{'host'} =~ m#/#)
#		|| !$Args->{'plugin'} || ($Args->{'plugin'} =~ m#/#)
#		|| (defined ($Args->{'plugin_instance'}) && ($Args->{'plugin_instance'} =~ m#/#))
#		|| !$Args->{'type'} || ($Args->{'type'} =~ m#/#)
#		|| (defined ($Args->{'type_instance'}) && ($Args->{'type_instance'} =~ m#/#)))
#	{
#		delete ($Args->{'host'});
#		delete ($Args->{'plugin'});
#		delete ($Args->{'plugin_instance'});
#		delete ($Args->{'type'});
#		delete ($Args->{'type_instance'});
#	}
#	elsif ($Args->{'type_instance'} eq '*')
#	{
#		my $subdir = $Args->{'host'} . '/'
#		. $Args->{'plugin'} . (defined ($Args->{'plugin_instance'}) ? ('-' . $Args->{'plugin_instance'}) : '');
#		my %type_instances = ();
#		my $regex_str = '^' . quotemeta ($Args->{'type'}) . '-(.*)\.rrd';
#		my $regex = qr/$regex_str/;
#		my $mtime = 0;
#
#		for (my $i = 0; $i < @DataDirs; $i++)
#		{
#			my $dh;
#			my @files = ();
#			opendir ($dh, $DataDirs[$i] . '/' . $subdir) or next;
#			for (readdir ($dh))
#			{
#				my $file = $_;
#				if ($file =~ $regex)
#				{
#					my $tmp_mtime;
#					my $type_instance = $1;
#					my $filename = $DataDirs[$i] . '/' . $subdir . '/' . $Args->{'type'} . '-' . $type_instance . '.rrd';
#
#					$type_instances{$type_instance} = [] if (!exists ($type_instances{$type_instance}));
#					$tmp_mtime = (stat ($filename))[9];
#					next if (!$tmp_mtime);
#
#					push (@{$type_instances{$type_instance}}, $filename);
#					$mtime = $tmp_mtime if ($mtime < $tmp_mtime);
#				}
#			}
#			closedir ($dh);
#		}
#
#		if (!keys %type_instances)
#		{
#			print STDOUT header (-Status => '404 File not found', -Content_Type => 'text/plain');
#			print STDOUT <<MSG;
#Sorry, the requested file could not be found anywhere.
#DataDirs = ${\join ('; ', @DataDirs)}
#subdir   = $subdir
#MSG
#			exit (0);
#		}
#
#		$Args->{'files'} = \%type_instances;
#		$Args->{'mtime'} = $mtime;
#	}
#	else
#	{
#		my $filename = $Args->{'host'} . '/'
#		. $Args->{'plugin'} . (defined ($Args->{'plugin_instance'}) ? ('-' . $Args->{'plugin_instance'}) : '') . '/'
#		. $Args->{'type'} . (defined ($Args->{'type_instance'}) ? ('-' . $Args->{'type_instance'}) : '') . '.rrd';
#		my @files = ();
#		my $mtime = 0;
#
#		for (my $i = 0; $i < @DataDirs; $i++)
#		{
#			my $tmp_file;
#			my $tmp_mtime;
#			$tmp_file = $DataDirs[$i] . '/' . $filename;
#			next if (!-e $tmp_file);
#			$tmp_mtime = (stat ($tmp_file))[9];
#			next if (!$tmp_mtime);
#
#			push (@files, $tmp_file);
#			$mtime = $tmp_mtime if ($mtime < $tmp_mtime);
#		}
#
#		if (!@files)
#		{
#			print STDOUT header (-Status => '404 File not found', -Content_Type => 'text/plain');
#			print STDOUT <<MSG;
#Sorry, the requested file could not be found anywhere.
#DataDirs = ${\join ('; ', @DataDirs)}
#filename = $filename
#MSG
#			exit (0);
#		}
#		$Args->{'files'} = \@files;
#		$Args->{'mtime'} = $mtime;
#	}
#
#	if ($Args->{'timespan'})
#	{
#		if ($Args->{'timespan'} =~ m/^([0-9]+)$/)
#		{
#			$Args->{'timespan'} = (-1) * $1;
#		}
#		elsif ($Args->{'timespan'} =~ m/^(hour|day|week|month|year)$/)
#		{
#			my %map =
#			(
#				hour => -3600,
#				day => -86400,
#				week => 7 * -86400,
#				month => 31 * -86400,
#				year => 366 * -86400
#			);
#			$Args->{'timespan'} = $map{$1};
#		}
#		else
#		{
#			$Args->{'timespan'} = -86400;
#		}
#	}
#} # validate_args

sub validate_args
{
	if ($Args->{'action'} && ($Args->{'action'} =~ m/^(overview|show_host|show_plugin|show_type|show_graph)$/))
	{
		$Args->{'action'} = $1;
	}
	else
	{
		$Args->{'action'} = 'overview';
	}

	if ($Args->{'host'} && ($Args->{'host'} =~ m#/#))
	{
		delete ($Args->{'host'});
	}

	if ($Args->{'plugin'} && ($Args->{'plugin'} =~ m#/#))
	{
		delete ($Args->{'plugin'});
	}

	if ($Args->{'type'} && ($Args->{'type'} =~ m#/#))
	{
		delete ($Args->{'type'});
	}

	if (!$Args->{'plugin'} || ($Args->{'plugin_instance'}
		&& ($Args->{'plugin_instance'} =~ m#/#)))
	{
		delete ($Args->{'plugin_instance'});
	}

	if (!$Args->{'type'} || ($Args->{'type_instance'}
		&& ($Args->{'type_instance'} =~ m#/#)))
	{
		delete ($Args->{'type_instance'});
	}

	if (defined ($Args->{'timespan'})
	  && ($Args->{'timespan'} =~ m/^(hour|day|week|month|year)$/))
	{
	  $Args->{'timespan'} = $1;
	}
	else
	{
	  $Args->{'timespan'} = 'day';
	}
} # validate_args

sub _find_hosts
{
  my %hosts = ();

  for (my $i = 0; $i < @DataDirs; $i++)
  {
    my @tmp;
    my $dh;

    opendir ($dh, $DataDirs[$i]) or next;
    @tmp = grep { ($_ !~ m/^\./) && (-d $DataDirs[$i] . '/' . $_) } (readdir ($dh));
    closedir ($dh);

    $hosts{$_} = 1 for (@tmp);
  } # for (@DataDirs)

  return (keys %hosts);
} # _find_hosts

sub _find_plugins
{
  my $host = shift;
  my %plugins = ();

  for (my $i = 0; $i < @DataDirs; $i++)
  {
    my $dir = $DataDirs[$i] . "/$host";
    my @tmp;
    my $dh;

    opendir ($dh, $dir) or next;
    @tmp = grep { ($_ !~ m/^\./) && (-d "$dir/$_") } (readdir ($dh));
    closedir ($dh);

    for (@tmp)
    {
      my ($plugin, $instance) = split (m/-/, $_, 2);
      $plugins{$plugin} = [] if (!$plugins{$plugin});
      push (@{$plugins{$plugin}}, $instance) if (defined ($instance));
    }
  } # for (@DataDirs)

  return (%plugins);
} # _find_hosts

sub _find_types
{
  my $host = shift;
  my $plugin = shift;
  my $plugin_instance = shift;
  my %types = ();

  for (my $i = 0; $i < @DataDirs; $i++)
  {
    my $dir = $DataDirs[$i] . "/$host/$plugin" . (defined ($plugin_instance) ? "-$plugin_instance" : '');
    my @tmp;
    my $dh;

    opendir ($dh, $dir) or next;
    @tmp = grep { ($_ !~ m/^\./) && ($_ =~ m/\.rrd$/i) && (-f "$dir/$_") } (readdir ($dh));
    closedir ($dh);

    for (@tmp)
    {
      my $name = "$_";
      $name =~ s/\.rrd$//i;
      my ($type, $instance) = split (m/-/, $name, 2);
      $types{$type} = [] if (!$types{$type});
      push (@{$types{$type}}, $instance) if (defined ($instance));
    }
  } # for (@DataDirs)

  return (%types);
} # _find_types

# sub _find_files_plugin
# {
# 	my $host = shift;
# 	my $plugin = shift;
# 	my $plugin_instance = shift;
# 	my $type = shift;
# 	my $type_instance = shift;
# 
# 	my @plugins = ();
# 	my %files = ();
# 
# 	if (!$plugin || ($plugin eq '*'))
# 	{
# 	}
# 	else
# 	{
# 		@plugins = ($plugin);
# 	}
# } # _find_files_plugin
# 
# sub _find_files_host
# {
# 	my $host = shift;
# 	my $plugin = shift;
# 	my $plugin_instance = shift;
# 	my $type = shift;
# 	my $type_instance = shift;
# 
# 	my @hosts = ();
# 	my %files = ();
# 
# 	if (!$host || ($host eq '*'))
# 	{
# 		my %hosts;
# 		for (my $i = 0; $i < @DataDirs; $i++)
# 		{
# 			my @tmp;
# 			my $dh;
# 			
# 			opendir ($dh, $DataDirs[$i]) or next;
# 			@tmp = grep { ($_ !~ m/^\./) && (-d $_) } (readdir ($dh));
# 			closedir ($dh);
# 
# 			$hosts{$_} = 1 for (@tmp);
# 		} # for (@DataDirs)
# 		@hosts = keys %hosts;
# 	}
# 	else
# 	{
# 		@hosts = ($host);
# 	}
# 
# 	for (my $i = 0; $i < @hosts; $i++)
# 	{
# 		my @files = _find_files_plugin ($hosts[$i], $plugin,
# 			$plugin_instance, $type, $type_instance);
# 		$files{$_} = 1 for (@files);
# 	}
# 
# 	return (wantarray () ? keys %files : [keys %files]);
# } # _find_files_host
# 
# sub _find_files
# {
#   my $host = shift;
#   my $plugin = shift;
#   my $plugin_instance = shift;
#   my $type = shift;
#   my $type_instance = shift;
# 
#   if ($host eq '*')
#   {
#     my @hosts = _find_all_hosts ();
# 
#   for (my $i = 0; $i < @DataDirs; $i++)
#   {
# 
#   } # for (i)
# } # _find_files

sub list_hosts
{
  my @hosts = _find_hosts ();
  @hosts = sort (@hosts);

  print "<ul>\n";
  for (my $i = 0; $i < @hosts; $i++)
  {
    my $host_html = encode_entities ($hosts[$i]);
    my $host_url = uri_escape ($hosts[$i]);

    print qq(  <li><a href="${\script_name ()}?action=show_host;host=$host_url">$host_html</a></li>\n);
  }
  print "</ul>\n";
} # list_hosts

sub action_show_host
{
  my $host = shift;
  my $host_url = uri_escape ($host);
  my %plugins = _find_plugins ($host);

  print qq(    <div><a href="${\script_name ()}?action=overview">Back to list of hosts</a></div>\n);

  print "<ul>\n";
  for (sort (keys %plugins))
  {
    my $plugin = $_;
    my $plugin_html = encode_entities ($plugin);
    my $plugin_url = uri_escape ($plugin);

    for (@{$plugins{$plugin}})
    {
      my $instance = $_;
      my $instance_html = encode_entities ($instance);
      my $instance_url = uri_escape ($instance);

      print qq#  <li><a href="${\script_name ()}?action=show_plugin;host=$host_url;plugin=$plugin_url;plugin_instance=$instance_url">$plugin_html ($instance_html)</a></li>\n#;
    }

    if (!@{$plugins{$plugin}})
    {
      print qq#  <li><a href="${\script_name ()}?action=show_plugin;host=$host_url;plugin=$plugin_url">$plugin_html</a></li>\n#;
    }
  } # for (%plugins)
  print "</ul>\n";
} # action_show_host

sub action_show_plugin
{
  my $host = shift;
  my $plugin = shift;
  my $plugin_instance = shift;

  my $host_url = uri_escape ($host);
  my $plugin_url = uri_escape ($plugin);
  my $plugin_instance_url = defined ($plugin_instance) ? uri_escape ($plugin_instance) : undef;

  my %types = _find_types ($host, $plugin, $plugin_instance);

  my $url_prefix = script_name () . "?host=$host_url;plugin=$plugin_url";
  $url_prefix .= ";plugin_instance=$plugin_instance_url" if (defined ($plugin_instance));

  print qq(    <div><a href="${\script_name ()}?action=show_host;host=$host_url">Back to list of plugins</a></div>\n);

  for (sort (keys %types))
  {
    my $type = $_;
    my $type_html = encode_entities ($type);
    my $type_url = uri_escape ($type);

    if (!defined ($GraphDefs->{$type}))
    {
      print qq#  <div><em>Unknown type &quot;$type_html&quot;</em></div>\n#;
      next;
    }

    for (@{$types{$type}})
    {
      my $instance = $_;
      my $instance_html = encode_entities ($instance);
      my $instance_url = uri_escape ($instance);

      print qq#  <div><a href="$url_prefix;type=$type_url;type_instance=$instance_url;action=show_type"><img src="$url_prefix;type=$type_url;type_instance=$instance_url;action=show_graph" /></a></div>\n#;
    }

    if (!@{$types{$type}})
    {
      print qq#  <div><a href="$url_prefix;type=$type_url;action=show_type"><img src="$url_prefix;type=$type_url;action=show_graph" /></a></div>\n#;
    }
  }
} # action_show_plugin

sub action_show_type
{
  my $host = shift;
  my $plugin = shift;
  my $plugin_instance = shift;
  my $type = shift;
  my $type_instance = shift;

  my $host_url = uri_escape ($host);
  my $plugin_url = uri_escape ($plugin);
  my $plugin_html = encode_entities ($plugin);
  my $plugin_instance_url = defined ($plugin_instance) ? uri_escape ($plugin_instance) : undef;
  my $type_url = uri_escape ($type);
  my $type_instance_url = defined ($type_instance) ? uri_escape ($type_instance) : undef;

  my $url_prefix = script_name () . "?action=show_plugin;host=$host_url;plugin=$plugin_url";
  $url_prefix .= ";plugin_instance=$plugin_instance_url" if (defined ($plugin_instance));

  print qq(    <div><a href="$url_prefix">Back to plugin &quot;$plugin_html&quot;</a></div>\n);

  $url_prefix = script_name () . "?action=show_graph;host=$host_url;plugin=$plugin_url";
  $url_prefix .= ";plugin_instance=$plugin_instance_url" if (defined ($plugin_instance));
  $url_prefix .= ";type=$type_url";
  $url_prefix .= ";type_instance=$type_instance_url" if (defined ($type_instance));

  for (qw(hour day week month year))
  {
    my $timespan = $_;

    print qq#  <div><img src="$url_prefix;timespan=$timespan" /></div>\n#;
  }
} # action_show_type

sub action_show_graph
{
  my $host = shift;
  my $plugin = shift;
  my $plugin_instance = shift;
  my $type = shift;
  my $type_instance = shift;
  my @rrd_args;
  my $title;
  
  my %times = (hour => -3600, day => -86400, week => 7 * -86400, month => 31 * -86400, year => 366 * -86400);
  my $start_time = $times{$Args->{'timespan'}} || -86400;

  #print STDERR Data::Dumper->Dump ([$Args], ['Args']);

  return if (!defined ($GraphDefs->{$type}));
  @rrd_args = @{$GraphDefs->{$type}};

  $title = "$host/$plugin" . (defined ($plugin_instance) ? "-$plugin_instance" : '')
  . "/$type" . (defined ($type_instance) ? "-$type_instance" : '');

  for (my $i = 0; $i < @DataDirs; $i++)
  {
    my $file = $DataDirs[$i] . "/$title.rrd";
    next if (!-f $file);

    s/{file}/$file/ for (@rrd_args);

    RRDs::graph ('-', '-a', 'PNG', '-s', $start_time, '-t', $title, @RRDDefaultArgs, @rrd_args);
    if (my $err = RRDs::error ())
    {
      die ("RRDs::graph: $err");
    }
  }
} # action_show_graph

sub print_header
{
  print <<HEAD;
Content-Type: application/xhtml+xml; charset=utf-8
Cache-Control: no-cache

<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN"
  "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">

<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">
  <head>
    <title>collection.cgi, Version 2</title>
    <style type="text/css">
      img
      {
	border: none;
      }
    </style>
  </head>

  <body>
HEAD
} # print_header

sub print_footer
{
  print <<FOOT;
  </body>
</html>
FOOT
} # print_footer

sub main
{
	read_config ();
	validate_args ();

	if (defined ($Args->{'host'})
	  && defined ($Args->{'plugin'})
	  && defined ($Args->{'type'})
	  && ($Args->{'action'} eq 'show_graph'))
	{
	  $| = 1;
	  print STDOUT header (-Content_Type => 'image/png');
	  action_show_graph ($Args->{'host'},
	    $Args->{'plugin'}, $Args->{'plugin_instance'},
	    $Args->{'type'}, $Args->{'type_instance'});
	  return (0);
	}

	print_header ();

	if (!$Args->{'host'})
	{
	  list_hosts ();
	}
	elsif (!$Args->{'plugin'})
	{
	  action_show_host ($Args->{'host'});
	}
	elsif (!$Args->{'type'})
	{
	  action_show_plugin ($Args->{'host'},
	    $Args->{'plugin'}, $Args->{'plugin_instance'});
	}
	else
	{
	  action_show_type ($Args->{'host'},
	    $Args->{'plugin'}, $Args->{'plugin_instance'},
	    $Args->{'type'}, $Args->{'type_instance'});
	}

	print_footer ();

	return (0);
}

sub load_graph_definitions
{
  my $Canvas = 'FFFFFF';

  my $FullRed    = 'FF0000';
  my $FullGreen  = '00E000';
  my $FullBlue   = '0000FF';
  my $FullYellow = 'F0A000';
  my $FullCyan   = '00A0FF';
  my $FullMagenta= 'A000FF';

  my $HalfRed    = 'F7B7B7';
  my $HalfGreen  = 'B7EFB7';
  my $HalfBlue   = 'B7B7F7';
  my $HalfYellow = 'F3DFB7';
  my $HalfCyan   = 'B7DFF7';
  my $HalfMagenta= 'DFB7F7';

  my $HalfBlueGreen = '89B3C9';

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
    'CDEF:moving_average=PREV,UN,avg,PREV,IF,0.8,*,avg,0.2,*,+',
    "AREA:max#$HalfBlue",
    "AREA:min#$Canvas",
    'LINE1:moving_average#000000',
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
    cpu => ['-v', 'CPU load',
    'DEF:avg={file}:value:AVERAGE',
    'DEF:min={file}:value:MIN',
    'DEF:max={file}:value:MAX',
    "AREA:max#$HalfBlue",
    "AREA:min#$Canvas",
    "LINE1:avg#$FullBlue:Percent",
    'GPRINT:min:MIN:%6.2lf%% Min,',
    'GPRINT:avg:AVERAGE:%6.2lf%% Avg,',
    'GPRINT:max:MAX:%6.2lf%% Max,',
    'GPRINT:avg:LAST:%6.2lf%% Last\l'
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
    df => ['-v', 'Percent',
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
    disk_octets => ['-v', 'Bytes/s',
    'DEF:out_min={file}:write:MIN',
    'DEF:out_avg={file}:write:AVERAGE',
    'DEF:out_max={file}:write:MAX',
    'DEF:inc_min={file}:read:MIN',
    'DEF:inc_avg={file}:read:AVERAGE',
    'DEF:inc_max={file}:read:MAX',
    'CDEF:overlap=out_avg,inc_avg,GT,inc_avg,out_avg,IF',
    'CDEF:mytime=out_avg,TIME,TIME,IF',
    'CDEF:sample_len_raw=mytime,PREV(mytime),-',
    'CDEF:sample_len=sample_len_raw,UN,0,sample_len_raw,IF',
    'CDEF:out_avg_sample=out_avg,UN,0,out_avg,IF,sample_len,*',
    'CDEF:out_avg_sum=PREV,UN,0,PREV,IF,out_avg_sample,+',
    'CDEF:inc_avg_sample=inc_avg,UN,0,inc_avg,IF,sample_len,*',
    'CDEF:inc_avg_sum=PREV,UN,0,PREV,IF,inc_avg_sample,+',
    "AREA:out_avg#$HalfGreen",
    "AREA:inc_avg#$HalfBlue",
    "AREA:overlap#$HalfBlueGreen",
    "LINE1:out_avg#$FullGreen:Written",
    'GPRINT:out_avg:AVERAGE:%5.1lf%s Avg,',
    'GPRINT:out_max:MAX:%5.1lf%s Max,',
    'GPRINT:out_avg:LAST:%5.1lf%s Last',
    'GPRINT:out_avg_sum:LAST:(ca. %5.1lf%sB Total)\l',
    "LINE1:inc_avg#$FullBlue:Read   ",
    'GPRINT:inc_avg:AVERAGE:%5.1lf%s Avg,',
    'GPRINT:inc_max:MAX:%5.1lf%s Max,',
    'GPRINT:inc_avg:LAST:%5.1lf%s Last',
    'GPRINT:inc_avg_sum:LAST:(ca. %5.1lf%sB Total)\l'
    ],
    disk_merged => ['-v', 'Merged Ops/s',
    'DEF:out_min={file}:write:MIN',
    'DEF:out_avg={file}:write:AVERAGE',
    'DEF:out_max={file}:write:MAX',
    'DEF:inc_min={file}:read:MIN',
    'DEF:inc_avg={file}:read:AVERAGE',
    'DEF:inc_max={file}:read:MAX',
    'CDEF:overlap=out_avg,inc_avg,GT,inc_avg,out_avg,IF',
    "AREA:out_avg#$HalfGreen",
    "AREA:inc_avg#$HalfBlue",
    "AREA:overlap#$HalfBlueGreen",
    "LINE1:out_avg#$FullGreen:Written",
    'GPRINT:out_avg:AVERAGE:%6.2lf Avg,',
    'GPRINT:out_max:MAX:%6.2lf Max,',
    'GPRINT:out_avg:LAST:%6.2lf Last\l',
    "LINE1:inc_avg#$FullBlue:Read   ",
    'GPRINT:inc_avg:AVERAGE:%6.2lf Avg,',
    'GPRINT:inc_max:MAX:%6.2lf Max,',
    'GPRINT:inc_avg:LAST:%6.2lf Last\l'
    ],
    disk_ops => ['-v', 'Ops/s',
    'DEF:out_min={file}:write:MIN',
    'DEF:out_avg={file}:write:AVERAGE',
    'DEF:out_max={file}:write:MAX',
    'DEF:inc_min={file}:read:MIN',
    'DEF:inc_avg={file}:read:AVERAGE',
    'DEF:inc_max={file}:read:MAX',
    'CDEF:overlap=out_avg,inc_avg,GT,inc_avg,out_avg,IF',
    "AREA:out_avg#$HalfGreen",
    "AREA:inc_avg#$HalfBlue",
    "AREA:overlap#$HalfBlueGreen",
    "LINE1:out_avg#$FullGreen:Written",
    'GPRINT:out_avg:AVERAGE:%6.2lf Avg,',
    'GPRINT:out_max:MAX:%6.2lf Max,',
    'GPRINT:out_avg:LAST:%6.2lf Last\l',
    "LINE1:inc_avg#$FullBlue:Read   ",
    'GPRINT:inc_avg:AVERAGE:%6.2lf Avg,',
    'GPRINT:inc_max:MAX:%6.2lf Max,',
    'GPRINT:inc_avg:LAST:%6.2lf Last\l'
    ],
    disk_time => ['-v', 'Seconds/s',
    'DEF:out_min_raw={file}:write:MIN',
    'DEF:out_avg_raw={file}:write:AVERAGE',
    'DEF:out_max_raw={file}:write:MAX',
    'DEF:inc_min_raw={file}:read:MIN',
    'DEF:inc_avg_raw={file}:read:AVERAGE',
    'DEF:inc_max_raw={file}:read:MAX',
    'CDEF:out_min=out_min_raw,1000,/',
    'CDEF:out_avg=out_avg_raw,1000,/',
    'CDEF:out_max=out_max_raw,1000,/',
    'CDEF:inc_min=inc_min_raw,1000,/',
    'CDEF:inc_avg=inc_avg_raw,1000,/',
    'CDEF:inc_max=inc_max_raw,1000,/',
    'CDEF:overlap=out_avg,inc_avg,GT,inc_avg,out_avg,IF',
    "AREA:out_avg#$HalfGreen",
    "AREA:inc_avg#$HalfBlue",
    "AREA:overlap#$HalfBlueGreen",
    "LINE1:out_avg#$FullGreen:Written",
    'GPRINT:out_avg:AVERAGE:%5.1lf%ss Avg,',
    'GPRINT:out_max:MAX:%5.1lf%ss Max,',
    'GPRINT:out_avg:LAST:%5.1lf%ss Last\l',
    "LINE1:inc_avg#$FullBlue:Read   ",
    'GPRINT:inc_avg:AVERAGE:%5.1lf%ss Avg,',
    'GPRINT:inc_max:MAX:%5.1lf%ss Max,',
    'GPRINT:inc_avg:LAST:%5.1lf%ss Last\l'
    ],
    dns_traffic => ['DEF:rsp_min_raw={file}:responses:MIN',
    'DEF:rsp_avg_raw={file}:responses:AVERAGE',
    'DEF:rsp_max_raw={file}:responses:MAX',
    'DEF:qry_min_raw={file}:queries:MIN',
    'DEF:qry_avg_raw={file}:queries:AVERAGE',
    'DEF:qry_max_raw={file}:queries:MAX',
    'CDEF:rsp_min=rsp_min_raw,8,*',
    'CDEF:rsp_avg=rsp_avg_raw,8,*',
    'CDEF:rsp_max=rsp_max_raw,8,*',
    'CDEF:qry_min=qry_min_raw,8,*',
    'CDEF:qry_avg=qry_avg_raw,8,*',
    'CDEF:qry_max=qry_max_raw,8,*',
    'CDEF:overlap=rsp_avg,qry_avg,GT,qry_avg,rsp_avg,IF',
    'CDEF:mytime=rsp_avg_raw,TIME,TIME,IF',
    'CDEF:sample_len_raw=mytime,PREV(mytime),-',
    'CDEF:sample_len=sample_len_raw,UN,0,sample_len_raw,IF',
    'CDEF:rsp_avg_sample=rsp_avg_raw,UN,0,rsp_avg_raw,IF,sample_len,*',
    'CDEF:rsp_avg_sum=PREV,UN,0,PREV,IF,rsp_avg_sample,+',
    'CDEF:qry_avg_sample=qry_avg_raw,UN,0,qry_avg_raw,IF,sample_len,*',
    'CDEF:qry_avg_sum=PREV,UN,0,PREV,IF,qry_avg_sample,+',
    "AREA:rsp_avg#$HalfGreen",
    "AREA:qry_avg#$HalfBlue",
    "AREA:overlap#$HalfBlueGreen",
    "LINE1:rsp_avg#$FullGreen:Responses",
    'GPRINT:rsp_avg:AVERAGE:%5.1lf%s Avg,',
    'GPRINT:rsp_max:MAX:%5.1lf%s Max,',
    'GPRINT:rsp_avg:LAST:%5.1lf%s Last',
    'GPRINT:rsp_avg_sum:LAST:(ca. %5.1lf%sB Total)\l',
    "LINE1:qry_avg#$FullBlue:Queries  ",
    #'GPRINT:qry_min:MIN:%5.1lf %s Min,',
    'GPRINT:qry_avg:AVERAGE:%5.1lf%s Avg,',
    'GPRINT:qry_max:MAX:%5.1lf%s Max,',
    'GPRINT:qry_avg:LAST:%5.1lf%s Last',
    'GPRINT:qry_avg_sum:LAST:(ca. %5.1lf%sB Total)\l'
    ],
    email => [
    'DEF:avg={file}:count:AVERAGE',
    'DEF:min={file}:count:MIN',
    'DEF:max={file}:count:MAX',
    "AREA:max#$HalfMagenta",
    "AREA:min#$Canvas",
    "LINE1:avg#$FullMagenta:Count ",
    'GPRINT:min:MIN:%4.1lf Min,',
    'GPRINT:avg:AVERAGE:%4.1lf Avg,',
    'GPRINT:max:MAX:%4.1lf Max,',
    'GPRINT:avg:LAST:%4.1lf Last\l'
    ],
    email_size => [
    'DEF:avg={file}:size:AVERAGE',
    'DEF:min={file}:size:MIN',
    'DEF:max={file}:size:MAX',
    "AREA:max#$HalfMagenta",
    "AREA:min#$Canvas",
    "LINE1:avg#$FullMagenta:Count ",
    'GPRINT:min:MIN:%4.1lf Min,',
    'GPRINT:avg:AVERAGE:%4.1lf Avg,',
    'GPRINT:max:MAX:%4.1lf Max,',
    'GPRINT:avg:LAST:%4.1lf Last\l'
    ],
    spam_score => ['-v', 'Score',
    'DEF:avg={file}:value:AVERAGE',
    'DEF:min={file}:value:MIN',
    'DEF:max={file}:value:MAX',
    "AREA:max#$HalfBlue",
    "AREA:min#$Canvas",
    "LINE1:avg#$FullBlue:Score ",
    'GPRINT:min:MIN:%4.1lf Min,',
    'GPRINT:avg:AVERAGE:%4.1lf Avg,',
    'GPRINT:max:MAX:%4.1lf Max,',
    'GPRINT:avg:LAST:%4.1lf Last\l'
    ],
    spam_check => [
    'DEF:avg={file}:hits:AVERAGE',
    'DEF:min={file}:hits:MIN',
    'DEF:max={file}:hits:MAX',
    "AREA:max#$HalfMagenta",
    "AREA:min#$Canvas",
    "LINE1:avg#$FullMagenta:Count ",
    'GPRINT:min:MIN:%4.1lf Min,',
    'GPRINT:avg:AVERAGE:%4.1lf Avg,',
    'GPRINT:max:MAX:%4.1lf Max,',
    'GPRINT:avg:LAST:%4.1lf Last\l'
    ],
    entropy => ['DEF:avg={file}:entropy:AVERAGE',
    'DEF:min={file}:entropy:MIN',
    'DEF:max={file}:entropy:MAX',
    "AREA:max#$HalfBlue",
    "AREA:min#$Canvas",
    "LINE1:avg#$FullBlue:Bits",
    'GPRINT:min:MIN:%4.0lfbit Min,',
    'GPRINT:avg:AVERAGE:%4.0lfbit Avg,',
    'GPRINT:max:MAX:%4.0lfbit Max,',
    'GPRINT:avg:LAST:%4.0lfbit Last\l'
    ],
    fanspeed => ['-v', 'RPM',
    'DEF:avg={file}:value:AVERAGE',
    'DEF:min={file}:value:MIN',
    'DEF:max={file}:value:MAX',
    "AREA:max#$HalfMagenta",
    "AREA:min#$Canvas",
    "LINE1:avg#$FullMagenta:RPM",
    'GPRINT:min:MIN:%4.1lf Min,',
    'GPRINT:avg:AVERAGE:%4.1lf Avg,',
    'GPRINT:max:MAX:%4.1lf Max,',
    'GPRINT:avg:LAST:%4.1lf Last\l'
    ],
    frequency => ['-v', 'Hertz',
    'DEF:avg={file}:frequency:AVERAGE',
    'DEF:min={file}:frequency:MIN',
    'DEF:max={file}:frequency:MAX',
    "AREA:max#$HalfBlue",
    "AREA:min#$Canvas",
    "LINE1:avg#$FullBlue:Frequency [Hz]",
    'GPRINT:min:MIN:%4.1lf Min,',
    'GPRINT:avg:AVERAGE:%4.1lf Avg,',
    'GPRINT:max:MAX:%4.1lf Max,',
    'GPRINT:avg:LAST:%4.1lf Last\l'
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
    "AREA:temp_max#$HalfRed",
    "AREA:temp_min#$Canvas",
    "LINE1:temp_avg#$FullRed:Temperature",
    'GPRINT:temp_min:MIN:%4.1lf Min,',
    'GPRINT:temp_avg:AVERAGE:%4.1lf Avg,',
    'GPRINT:temp_max:MAX:%4.1lf Max,',
    'GPRINT:temp_avg:LAST:%4.1lf Last\l'
    ],
    if_packets => ['-v', 'Packets/s',
    'DEF:tx_min={file}:tx:MIN',
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
    irq => ['-v', 'Issues/s',
    'DEF:avg={file}:value:AVERAGE',
    'DEF:min={file}:value:MIN',
    'DEF:max={file}:value:MAX',
    "AREA:max#$HalfBlue",
    "AREA:min#$Canvas",
    "LINE1:avg#$FullBlue:Issues/s",
    'GPRINT:min:MIN:%6.2lf Min,',
    'GPRINT:avg:AVERAGE:%6.2lf Avg,',
    'GPRINT:max:MAX:%6.2lf Max,',
    'GPRINT:avg:LAST:%6.2lf Last\l'
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
    'HRULE:0#000000'
    ],
    memory => ['-b', '1024', '-v', 'Bytes',
    'DEF:avg={file}:value:AVERAGE',
    'DEF:min={file}:value:MIN',
    'DEF:max={file}:value:MAX',
    "AREA:max#$HalfBlue",
    "AREA:min#$Canvas",
    "LINE1:avg#$FullBlue:Memory",
    'GPRINT:min:MIN:%5.1lf%sbyte Min,',
    'GPRINT:avg:AVERAGE:%5.1lf%sbyte Avg,',
    'GPRINT:max:MAX:%5.1lf%sbyte Max,',
    'GPRINT:avg:LAST:%5.1lf%sbyte Last\l'
    ],
    old_memory => [
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
    mysql_commands => ['-v', 'Issues/s',
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
    mysql_handler => ['-v', 'Issues/s',
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
    mysql_octets => ['-v', 'Bytes/s',
    'DEF:out_min={file}:tx:MIN',
    'DEF:out_avg={file}:tx:AVERAGE',
    'DEF:out_max={file}:tx:MAX',
    'DEF:inc_min={file}:rx:MIN',
    'DEF:inc_avg={file}:rx:AVERAGE',
    'DEF:inc_max={file}:rx:MAX',
    'CDEF:overlap=out_avg,inc_avg,GT,inc_avg,out_avg,IF',
    'CDEF:mytime=out_avg,TIME,TIME,IF',
    'CDEF:sample_len_raw=mytime,PREV(mytime),-',
    'CDEF:sample_len=sample_len_raw,UN,0,sample_len_raw,IF',
    'CDEF:out_avg_sample=out_avg,UN,0,out_avg,IF,sample_len,*',
    'CDEF:out_avg_sum=PREV,UN,0,PREV,IF,out_avg_sample,+',
    'CDEF:inc_avg_sample=inc_avg,UN,0,inc_avg,IF,sample_len,*',
    'CDEF:inc_avg_sum=PREV,UN,0,PREV,IF,inc_avg_sample,+',
    "AREA:out_avg#$HalfGreen",
    "AREA:inc_avg#$HalfBlue",
    "AREA:overlap#$HalfBlueGreen",
    "LINE1:out_avg#$FullGreen:Written",
    'GPRINT:out_avg:AVERAGE:%5.1lf%s Avg,',
    'GPRINT:out_max:MAX:%5.1lf%s Max,',
    'GPRINT:out_avg:LAST:%5.1lf%s Last',
    'GPRINT:out_avg_sum:LAST:(ca. %5.1lf%sB Total)\l',
    "LINE1:inc_avg#$FullBlue:Read   ",
    'GPRINT:inc_avg:AVERAGE:%5.1lf%s Avg,',
    'GPRINT:inc_max:MAX:%5.1lf%s Max,',
    'GPRINT:inc_avg:LAST:%5.1lf%s Last',
    'GPRINT:inc_avg_sum:LAST:(ca. %5.1lf%sB Total)\l'
    ],
    mysql_qcache => ['-v', 'Queries/s',
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
    mysql_threads => ['-v', 'Threads',
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
    nfs_procedure => ['-v', 'Issues/s',
    'DEF:avg={file}:value:AVERAGE',
    'DEF:min={file}:value:MIN',
    'DEF:max={file}:value:MAX',
    "AREA:max#$HalfBlue",
    "AREA:min#$Canvas",
    "LINE1:avg#$FullBlue:Issues/s",
    'GPRINT:min:MIN:%6.2lf Min,',
    'GPRINT:avg:AVERAGE:%6.2lf Avg,',
    'GPRINT:max:MAX:%6.2lf Max,',
    'GPRINT:avg:LAST:%6.2lf Last\l'
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
    opcode => [
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
    percent => ['-v', 'Percent',
    'DEF:avg={file}:percent:AVERAGE',
    'DEF:min={file}:percent:MIN',
    'DEF:max={file}:percent:MAX',
    "AREA:max#$HalfBlue",
    "AREA:min#$Canvas",
    "LINE1:avg#$FullBlue:Percent",
    'GPRINT:min:MIN:%5.1lf%% Min,',
    'GPRINT:avg:AVERAGE:%5.1lf%% Avg,',
    'GPRINT:max:MAX:%5.1lf%% Max,',
    'GPRINT:avg:LAST:%5.1lf%% Last\l'
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
    ps_state => ['-v', 'Processes',
    'DEF:avg={file}:value:AVERAGE',
    'DEF:min={file}:value:MIN',
    'DEF:max={file}:value:MAX',
    "AREA:max#$HalfBlue",
    "AREA:min#$Canvas",
    "LINE1:avg#$FullBlue:Processes",
    'GPRINT:min:MIN:%6.2lf Min,',
    'GPRINT:avg:AVERAGE:%6.2lf Avg,',
    'GPRINT:max:MAX:%6.2lf Max,',
    'GPRINT:avg:LAST:%6.2lf Last\l'
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
    rcode => [
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
    swap => ['-v', 'Bytes', '-b', '1024',
    'DEF:avg={file}:value:AVERAGE',
    'DEF:min={file}:value:MIN',
    'DEF:max={file}:value:MAX',
    "AREA:max#$HalfBlue",
    "AREA:min#$Canvas",
    "LINE1:avg#$FullBlue:Bytes",
    'GPRINT:min:MIN:%6.2lf%sByte Min,',
    'GPRINT:avg:AVERAGE:%6.2lf%sByte Avg,',
    'GPRINT:max:MAX:%6.2lf%sByte Max,',
    'GPRINT:avg:LAST:%6.2lf%sByte Last\l'
    ],
    ols_swap => [
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
    temperature => ['-v', 'Celsius',
    'DEF:temp_avg={file}:value:AVERAGE',
    'DEF:temp_min={file}:value:MIN',
    'DEF:temp_max={file}:value:MAX',
    "AREA:temp_max#$HalfRed",
    "AREA:temp_min#$Canvas",
    "LINE1:temp_avg#$FullRed:Temperature",
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
    if_octets => ['-v', 'Bits/s',
    'DEF:out_min_raw={file}:tx:MIN',
    'DEF:out_avg_raw={file}:tx:AVERAGE',
    'DEF:out_max_raw={file}:tx:MAX',
    'DEF:inc_min_raw={file}:rx:MIN',
    'DEF:inc_avg_raw={file}:rx:AVERAGE',
    'DEF:inc_max_raw={file}:rx:MAX',
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
    voltage => ['-v', 'Voltage',
    'DEF:avg={file}:value:AVERAGE',
    'DEF:min={file}:value:MIN',
    'DEF:max={file}:value:MAX',
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
} # load_graph_definitions
# vim: shiftwidth=2:softtabstop=2:tabstop=8
