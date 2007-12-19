EXTRA_DIST = perl/Collectd.pm perl/Makefile.PL perl/Collectd/Makefile.PL perl/Collectd/Unixsock.pm

PERL_SUBDIR = $(srcdir)/perl

all-local: @PERL_BINDINGS@

install-exec-local:
	[ ! -f $(PERL_SUBDIR)/Makefile ] || $(MAKE) -C $(PERL_SUBDIR) install

clean-local:
	[ ! -f $(PERL_SUBDIR)/Makefile ] || $(MAKE) -C $(PERL_SUBDIR) realclean

@srcdir@/perl: $(PERL_SUBDIR)/Makefile
	$(MAKE) -C $(PERL_SUBDIR)

@srcdir@/perl/Makefile: @srcdir@/perl/Makefile.PL @srcdir@/perl/Collectd/Makefile.PL
	cd $(PERL_SUBDIR) && @PERL@ Makefile.PL PREFIX=$(prefix) @PERL_BINDINGS_OPTIONS@

.PHONY: @srcdir@/perl

