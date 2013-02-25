#! /usr/bin/perl

use strict;
use ExtUtils::Installed;

my $mod = $ARGV[0] || die "Usage : $0 Module\n";
my $inst = ExtUtils::Installed->new();
unlink $inst->files($mod), $inst->packlist($mod)->packlist_file();
