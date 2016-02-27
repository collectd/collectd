#!/usr/bin/perl

use strict;
use warnings;

=head1 NAME

wiki2changelog.pl

=head1 DESCRIPTION

This script takes the change log from one of the "Version x.y" pages in
collectd's wiki and converts it to the format used by the "ChangeLog" file.
This is usually done as part of the release process.

=cut

our $TextWidth = 80;

sub format_entry
{
	my $in = shift;
	my $out = '';

	my $line = "\t*";
	my $line_len = 9;

	for (split (' ', $in)) {
		my $word = $_;
		my $word_len = 1 + length $word;

		if (($line_len + $word_len) > $TextWidth) {
			$out .= "$line\n";
			$line = "\t ";
			$line_len = 9;
		}

		$line .= " $word";
		$line_len += $word_len;
	}

	if ($line_len != 9) {
		$out .= "$line\n";
	}

	return $out;
}

while (<>)
{
	chomp;
	my $line = $_;

	if ($line =~ m#^\* (.*)#) {
		$line = $1;
	} else {
		next;
	}

	$line =~ s#&lt;#<#g;
	$line =~ s#&gt;#>#g;
	$line =~ s#&nbsp;# #g;
	$line =~ s#&quot;#"#g;

	$line =~ s#\{\{Plugin\|([^}]+)\}\}#$1 plugin#g;
	$line =~ s@\{\{Issue\|([^}]+)\}\}@#$1@g;
	$line =~ s#\[\[[^|]+\|([^\]]+)\]\]#$1#g;
	$line =~ s#\[\[([^|]+)\]\]#$1#g;

	$line =~ s#'''(.*?)'''#*$1*#g;
	$line =~ s#''(.*?)''#$1#g;
	$line =~ s#<code>(.*?)</code>#"$1"#gi;

	print format_entry($line);
}
