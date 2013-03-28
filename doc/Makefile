include ../common.mak

all: 

install: install-man

uninstall: uninstall-man

install-man: harvid.1
	install -d $(DESTDIR)$(mandir)
	install -m644 harvid.1 $(DESTDIR)$(mandir)

uninstall-man:
	rm -f $(DESTDIR)$(mandir)/harvid.1
	-rmdir $(DESTDIR)$(mandir)

install-bin:

uninstall-bin:

man:

clean:

.PHONY: all install uninstall install-man uninstall-man install-bin uninstall-bin
