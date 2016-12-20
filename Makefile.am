ACLOCAL_AMFLAGS = -I m4

SUBDIRS = proto src bindings .

EXTRA_DIST = contrib version-gen.sh testwrapper.sh

install-exec-hook:
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/run
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/lib/$(PACKAGE_NAME)
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/log

maintainer-clean-local:
	-rm -f INSTALL
	-rm -f aclocal.m4
