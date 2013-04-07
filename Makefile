VERSION?=$(shell git describe --tags HEAD || echo "X.X.X")
SUBDIRS = libharvid src doc

default: all

$(SUBDIRS)::
	$(MAKE) -C $@ $(MAKECMDGOALS)

all clean man install uninstall install-bin install-man uninstall-bin uninstall-man install-lib uninstall-lib: $(SUBDIRS)

dist:
	git archive --format=tar --prefix=harvid-$(VERSION)/ HEAD | gzip -9 > harvid-$(VERSION).tar.gz

.PHONY: clean all subdirs install uninstall dist install-bin install-man uninstall-bin uninstall-man install-lib uninstall-lib
