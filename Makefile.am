ACLOCAL_AMFLAGS = -I libltdl/m4

SUBDIRS =

if BUILD_INCLUDED_LTDL
SUBDIRS += libltdl
endif

SUBDIRS += proto src bindings .

AM_CPPFLAGS = $(LTDLINCL)

EXTRA_DIST = contrib version-gen.sh testwrapper.sh

install-exec-hook:
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/run
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/lib/$(PACKAGE_NAME)
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/log

maintainer-clean-local:
	-rm -f -r libltdl
	-rm -f INSTALL
	-rm -f aclocal.m4
