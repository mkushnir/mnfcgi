# consider headers:
#   - include_HEADERS
#   - nobase_include_HEADERS
#   - pkginclude_HEADERS
#   - dist_HEADERS
#   - nodist_HEADERS
#   - noinst_HEADERS

BUILT_SOURCES = diag.c diag.h
EXTRA_DIST = $(diags) gen-diag
CLEANFILES = $(BUILT_SOURCES) *.core core
#CLEANFILES += *.in
AM_MAKEFLAGS = -s

lib_LTLIBRARIES = libmnfcgi.la
noinst_HEADERS= mnfcgi_private.h mnfcgi_app_private.h
nobase_include_HEADERS = mnfcgi.h mnfcgi_app.h

libmnfcgi_la_SOURCES = mnfcgi_wire.c mnfcgi_proto.c mnfcgi_util.c mnfcgi_app.c
nodist_libmnfcgi_la_SOURCES = diag.c

diags = diag.txt

if DEBUG
DEBUG_FLAGS = -g -O0 @CC_DEBUG@
else
DEBUG_FLAGS = -DNDEBUG -O3
endif

libmnfcgi_la_CFLAGS = $(DEBUG_FLAGS) -Wall -Wextra -Werror -std=c99 @_GNU_SOURCE_MACRO@ -I$(top_srcdir)/src -I$(top_srcdir) -I$(includedir)

#libmnfcgi_la_LDFLAGS = -version-info 0:0:0
libmnfcgi_la_LDFLAGS = -version-info 0:0:0 -L$(libdir) -lmrkapp -lmrkthr -lmrkcommon
#libmnfcgi_la_LDFLAGS = -all-static
#libmnfcgi_la_LDFLAGS = -all-static -Wl,-Bdynamic,-L$(libdir),-lfoo -lqwe,-Bstatic

diag.c diag.h: $(diags) gen-diag
	$(AM_V_GEN) cat $(diags) | sort -u | ./gen-diag mnfcgi

run: all
	for i in $(bin_PROGRAMS); do if test -x ./$$i; then LD_LIBRARY_PATH=$(libdir) ./$$i; fi; done;

testrun:
	for i in $(SUBDIRS); do if test "$$i" != "."; then cd $$i && $(MAKE) testrun && cd ..; fi; done;
