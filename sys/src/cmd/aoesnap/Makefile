# this works in gnu make
SYSNAME:=${shell uname}
OBJTYPE:=${shell uname -m | sed 's;i.86;386;; s;/.*;;; s; ;;g'}

# this works in bsd make
SYSNAME!=uname
OBJTYPE!=uname -m | sed 's;i.86;386;; s;amd64;x864_64;; s;/.*;;; s; ;;g'

# the gnu rules will mess up bsd but not vice versa,
# hence the gnu rules come first.

RANLIB=true

include Make.$(SYSNAME)-$(OBJTYPE)

PREFIX=/usr/local

OFILES=\
	aoe.$O\
	bio.$O\
	goo.$O\
	print.$O\
	take.$O\
	util.$O\

HFILES=\
	goo.h\
	snap.h\
	u.h\

TARGETS=$O.aoesnap\

all: $(TARGETS)

install: $(TARGETS)
	mkdir -p $(PREFIX)/man/man8
	install -m 0644 aoesnap.man8 $(PREFIX)/man/man/aoesnap.8
	mkdir -p $(PREFIX)/bin
	for i in $(TARGETS); do \
		j=`echo $$i|sed 's:$O\.::'`;\
		install -m 0755 $$i $(PREFIX)/bin/$$j;\
	done

$O.aoesnap: $(OFILES) $(HFILES) aoesnap.$O
	$(CC) -o $O.aoesnap aoesnap.$O $(OFILES)

.c.$O: $(HFILES)
	$(CC) $(CFLAGS) $*.c

%.$O: %.c $(HFILES)
	$(CC) $(CFLAGS) $*.c

$(OFILES): $(HFILES)

clean:
	rm -f $(TARGETS) $(OFILES) $(LIB) $(CLEANFILES)

nuke:
	rm -f $(TARGETS) $(OFILES) *.tgz *.rpm $(NUKEFILES)

.phony: all clean nuke install
