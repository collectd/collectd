SUBDIRS =

if BUILD_WITH_JAVA
SUBDIRS += java
endif

EXTRA_DIST = perl/Makefile.PL \
	     perl/uninstall_mod.pl \
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
	@PERL@ -I$(DESTDIR)$(prefix) $(srcdir)/perl/uninstall_mod.pl Collectd
	find $(DESTDIR)$(prefix) -name "perllocal.pod" -exec rm {} \;

clean-local:
	rm -rf buildperl

perl: buildperl/Makefile
	cd buildperl && $(MAKE)

buildperl/Makefile: .perl-directory-stamp buildperl/Makefile.PL \
	$(top_builddir)/config.status
	@# beautify the output a bit
	@echo 'cd buildperl && @PERL@ Makefile.PL @PERL_BINDINGS_OPTIONS@'
	@cd buildperl && ( if ! @PERL@ Makefile.PL @PERL_BINDINGS_OPTIONS@; then \
			echo ""; \
			echo 'Check whether you have set $$PERL_MM_OPT in your environment and try using ./configure --with-perl-bindings=""'; \
			echo ""; \
		fi )

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

