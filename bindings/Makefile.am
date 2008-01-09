EXTRA_DIST = perl/Collectd.pm perl/Makefile.PL perl/Collectd/Makefile.PL perl/Collectd/Unixsock.pm

all-local: @PERL_BINDINGS@

install-exec-local:
	[ ! -f perl/Makefile ] || ( cd perl && $(MAKE) install )

clean-local:
	[ ! -f perl/Makefile ] || ( cd perl && $(MAKE) realclean )

perl: perl/Makefile
	cd perl && $(MAKE)

perl/Makefile: .perl-directory-stamp perl/Makefile.PL perl/Collectd/Makefile.PL
	cd perl && @PERL@ Makefile.PL PREFIX=$(prefix) @PERL_BINDINGS_OPTIONS@

.perl-directory-stamp:
	if test ! -d perl; then \
	  mkdir -p perl/Collectd; \
	  cp $(srcdir)/perl/Collectd.pm perl/; \
	  cp $(srcdir)/perl/Makefile.PL perl/; \
	  cp $(srcdir)/perl/Collectd/Unixsock.pm perl/Collectd/; \
	  cp $(srcdir)/perl/Collectd/Makefile.PL perl/Collectd/; \
	fi
	touch $@

.PHONY: perl

