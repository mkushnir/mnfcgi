CLEANFILES = *.core core
#CLEANFILES += *.in
AM_MAKEFLAGS = -s

# consider headers:
#   - include_HEADERS
#   - nobase_include_HEADERS
#   - pkginclude_HEADERS
#   - dist_HEADERS
#   - nodist_HEADERS
#   - noinst_HEADERS
noinst_HEADERS = unittest.h testmy.h testoauth.h

noinst_PROGRAMS=gendata testfoo
if MRKPQ
noinst_PROGRAMS+= testbar
endif

BUILT_SOURCES = diag.c diag.h
EXTRA_DIST = diag.txt runscripts

#noinst_HEADERS = ../src/mnfcgi.h

diags = ../src/diag.txt diag.txt

if DEBUG
DEBUG_FLAGS = -g -O0 @CC_DEBUG@
else
DEBUG_FLAGS = -DNDEBUG -O3
endif

nodist_gendata_SOURCES = diag.c
gendata_SOURCES = gendata.c
gendata_CFLAGS = $(DEBUG_FLAGS) -Wall -Wextra -Werror -std=c99 @_GNU_SOURCE_MACRO@ -I$(top_srcdir)/test -I$(top_srcdir)/src -I$(top_srcdir) -I$(includedir)
gendata_LDFLAGS = -L$(libdir) -lmrkcommon -lmndiag
#gendata_LDFLAGS = 

nodist_testfoo_SOURCES = diag.c
testfoo_SOURCES = ../src/mnfcgi_app.c testfoo.c
testfoo_CFLAGS = $(DEBUG_FLAGS) -DUNITTEST -Wall -Wextra -Werror -std=c99 @_GNU_SOURCE_MACRO@ -I$(top_srcdir)/test -I$(top_srcdir)/src -I$(top_srcdir) -I$(includedir)
testfoo_LDFLAGS = -L$(top_srcdir)/src/.libs -lmnfcgi -L$(libdir) -lmrkapp -lmrkthr -lmrkcommon -lmndiag
#testfoo_LDFLAGS = -L$(top_srcdir)/src/.libs -lmnfcgi

if MRKPQ
nodist_testbar_SOURCES = diag.c
testbar_SOURCES = testbar.c \
		  testoauth.c \
		  testoauth_login.c \
		  testoauth_token.c \
		  testoauth_client.c \
		  testoauth_util.c \
		  testoauth_da.c \
		  testmy.c

testbar_CFLAGS = $(DEBUG_FLAGS) -Wall -Wextra -Werror -std=c99 @_GNU_SOURCE_MACRO@ -I$(top_srcdir)/test -I$(top_srcdir)/src -I$(top_srcdir) -I$(includedir)
testbar_LDFLAGS = -L$(top_srcdir)/src/.libs -lmnfcgi -L$(libdir) -lmrkpq -lmrkthr -lmrkcommon -lmndiag
#testbar_LDFLAGS = -L$(top_srcdir)/src/.libs -lmnfcgi
endif

diag.c diag.h: $(diags)
	$(AM_V_GEN) cat $(diags) | sort -u >diag.txt.tmp && mndiagen -S diag.txt.tmp -L mnfcgi -H diag.h -C diag.c ../*.[ch] ./*.[ch]

testfoo-run: testfoo
	@if test -x $>; \
	    then LD_LIBRARY_PATH=$(libdir) ./$> $${$(>)_ARGS}; \
	    else true; \
	fi

testrun: all
	for i in $(noinst_PROGRAMS); do if test -x ./$$i; then LD_LIBRARY_PATH=$(libdir) ./$$i; fi; done;
