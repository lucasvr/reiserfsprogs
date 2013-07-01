sbin_PROGRAMS = debugreiserfs

debugreiserfs_SOURCES = debugreiserfs.c pack.c unpack.c stat.c corruption.c scan.c recover.c debugreiserfs.h
man_MANS = debugreiserfs.8
EXTRA_DIST = $(man_MANS)

LDADD = $(top_builddir)/reiserfscore/libcore.a $(top_builddir)/lib/libmisc.a

install-exec-hook:
	$(LN_S) debugreiserfs $(DESTDIR)$(sbindir)/debugfs.reiserfs
