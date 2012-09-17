SUBDIRS =

if BUILD_WITH_JAVA
SUBDIRS += java
endif

EXTRA_DIST = perl/Makefile.PL \
	     perl/lib/Collectd.pm \
	     perl/lib/Collectd/Unixsock.pm \
	     perl/lib/Collectd/Plugins/Monitorus.pm \
	     perl/lib/Collectd/Plugins/OpenVZ.pm

CLEANFILES = \
	buildperl/Collectd.pm \
	buildperl/Collectd/Plugins/OpenVZ.pm \
	buildperl/Collectd/Unixsock.pm \
	buildperl/Makefile.PL \
	.perl-directory-stamp

DISTCLEANFILES = \
	buildperl/Collectd.pm \
	buildperl/Collectd/Plugins/OpenVZ.pm \
	buildperl/Collectd/Unixsock.pm \
	buildperl/Makefile.PL \
	.perl-directory-stamp

all-local: @PERL_BINDINGS@


install-exec-local:
	[ ! -f buildperl/Makefile ] || ( cd buildperl && $(MAKE) install )

# Perl 'make uninstall' does not work as well as wanted.
# So we do the work here.
uninstall-local:
	rm -f $(DESTDIR)$(mandir)/man3/Collectd::Unixsock.3pm
	rm -f $(DESTDIR)$(datarootdir)/perl5/Collectd.pm
	rm -f $(DESTDIR)$(datarootdir)/perl5/Collectd/Plugins/OpenVZ.pm
	rm -f $(DESTDIR)$(datarootdir)/perl5/Collectd/Unixsock.pm
	rm -f $(DESTDIR)$(prefix)/lib64/perl5/perllocal.pod
	rm -f $(DESTDIR)$(prefix)/lib64/perl5/auto/Collectd/.packlist

clean-local:
	rm -rf buildperl

perl: buildperl/Makefile
	cd buildperl && $(MAKE)

buildperl/Makefile: .perl-directory-stamp buildperl/Makefile.PL \
	$(top_builddir)/config.status
	cd buildperl && @PERL@ Makefile.PL PREFIX=$(prefix) @PERL_BINDINGS_OPTIONS@

buildperl/Makefile.PL: .perl-directory-stamp $(top_builddir)/config.status

.perl-directory-stamp:
	if test ! -d buildperl; then \
	  mkdir -p buildperl/Collectd/Plugins; \
	  cp $(srcdir)/perl/lib/Collectd.pm buildperl/; \
	  cp $(srcdir)/perl/Makefile.PL buildperl/; \
	  cp $(srcdir)/perl/lib/Collectd/Unixsock.pm buildperl/Collectd/; \
	  cp $(srcdir)/perl/lib/Collectd/Plugins/OpenVZ.pm buildperl/Collectd/Plugins/; \
	fi
	touch $@

.PHONY: perl

