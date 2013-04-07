include ../common.mak

all: 

install: install-man

uninstall: uninstall-man

install-man: harvid.1
	install -d $(DESTDIR)$(man1dir)
	install -m644 harvid.1 $(DESTDIR)$(man1dir)

uninstall-man:
	rm -f $(DESTDIR)$(man1dir)/harvid.1
	-rmdir $(DESTDIR)$(man1dir)

install-bin:

uninstall-bin:

install-lib:
	install -d $(DESTDIR)$(hdocdir)/examples
	install -m 644 libharvid_example.c $(DESTDIR)$(hdocdir)/examples/
	gzip -9 $(DESTDIR)$(hdocdir)/examples/libharvid_example.c

uninstall-lib:
	rm -f $(DESTDIR)$(hdocdir)/examples/libharvid_example.c.gz
	-rmdir $(DESTDIR)$(hdocdir)/examples
	-rmdir $(DESTDIR)$(hdocdir)
	-rmdir $(DESTDIR)$(docdir)

man:

clean:

.PHONY: all install uninstall install-man uninstall-man install-bin uninstall-bin install-lib uninstall-lib
