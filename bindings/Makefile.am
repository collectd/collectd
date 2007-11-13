EXTRA_DIST = perl/Collectd.pm perl/Makefile.PL perl/Collectd/Makefile.PL \
		perl/Collectd/Unixsock.pm

all-local: @PERL_BINDINGS@

install-exec-local:
	[ ! -f perl/Makefile ] || $(MAKE) -C perl install

clean-local:
	[ ! -f perl/Makefile ] || $(MAKE) -C perl realclean

perl: perl/Makefile
	$(MAKE) -C perl

perl/Makefile: perl/Makefile.PL perl/Collectd/Makefile.PL \
		$(top_builddir)/config.status
	cd perl && @PERL@ Makefile.PL PREFIX=$(prefix) @PERL_BINDINGS_OPTIONS@

.PHONY: perl

