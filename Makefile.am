ACLOCAL_AMFLAGS = -I libltdl/m4

SUBDIRS = libltdl src bindings

INCLUDES = $(LTDLINCL)

EXTRA_DIST = contrib version-gen.sh

install-exec-hook:
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/run
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/lib/$(PACKAGE_NAME)
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/log
