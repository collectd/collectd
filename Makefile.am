AUTOMAKE_OPTIONS = dist-bzip2

SUBDIRS = src

EXTRA_DIST = collectd.spec contrib debian

dist-hook:
	find $(distdir) -type d -name '.svn' | xargs rm -rf

install-exec-hook:
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/run
