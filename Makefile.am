SUBDIRS = libltdl src bindings

INCLUDES = $(LTDLINCL)

EXTRA_DIST = contrib version-gen.sh

dist-hook:
	find $(distdir) -type d -name '.svn' | xargs rm -rf

install-exec-hook:
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/run
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/lib/$(PACKAGE_NAME)
