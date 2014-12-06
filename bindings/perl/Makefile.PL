use ExtUtils::MakeMaker;

WriteMakefile(
	NAME			=> 'Collectd',
	AUTHOR			=> 'Sebastian Harl <sh@tokkee.org>',
	TEST_REQUIRES	=> { 'YAML::Any' => 0 },
);

# vim: set sw=4 ts=4 tw=78 noexpandtab :
