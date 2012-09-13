SUBDIRS =

if BUILD_WITH_JAVA
SUBDIRS += java
endif

EXTRA_DIST = perl/Makefile.PL \
	     perl/lib/Collectd.pm \
	     perl/lib/Collectd/Unixsock.pm \
	     perl/lib/Collectd/Plugins/Monitorus.pm \
	     perl/lib/Collectd/Plugins/OpenVZ.pm

all-local: @PERL_BINDINGS@

install-exec-local:
	[ ! -f perl/Makefile ] || ( cd perl && $(MAKE) install )

clean-local:
	[ ! -f perl/Makefile ] || ( cd perl && $(MAKE) realclean )

perl: perl/Makefile
	cd perl && $(MAKE)

perl/Makefile: .perl-directory-stamp perl/Makefile.PL \
	$(top_builddir)/config.status
	cd perl && @PERL@ Makefile.PL PREFIX=$(prefix) @PERL_BINDINGS_OPTIONS@

.perl-directory-stamp:
	if test ! -d perl; then \
	  mkdir -p perl/Collectd/Plugins; \
	  cp $(srcdir)/perl/Collectd.pm perl/; \
	  cp $(srcdir)/perl/Makefile.PL perl/; \
	  cp $(srcdir)/perl/Collectd/Unixsock.pm perl/Collectd/; \
	  cp $(srcdir)/perl/Collectd/Plugins/OpenVZ.pm perl/Collectd/Plugins/; \
	fi
	touch $@

.PHONY: perl

