sbin_PROGRAMS = debugreiserfs

debugreiserfs_SOURCES = debugreiserfs.c pack.c unpack.c stat.c corruption.c scan.c recover.c debugreiserfs.h
man_MANS = debugreiserfs.8
EXTRA_DIST = $(man_MANS)

LDADD = $(top_srcdir)/lib/libmisc.a $(top_srcdir)/reiserfscore/libcore.a
