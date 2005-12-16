SUBDIRS = libltdl src
INCLUDES = $(LTDLINCL)

EXTRA_DIST = collectd.spec contrib debian

dist-hook:
	find $(distdir) -type d -name '.svn' | xargs rm -rf

install-exec-hook:
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/run
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/lib/$(PACKAGE_NAME)
	$(mkinstalldirs) $(DESTDIR)$(sysconfdir)
