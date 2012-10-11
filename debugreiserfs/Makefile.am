sbin_PROGRAMS = debugreiserfs unpack

debugreiserfs_SOURCES = debugreiserfs.c pack.c stat.c corruption.c scan.c recover.c
unpack_SOURCES = unpack.c pack.c debugreiserfs.h
man_MANS = debugreiserfs.8
EXTRA_DIST = $(man_MANS)

LDADD = $(top_srcdir)/lib/libmisc.a $(top_srcdir)/reiserfscore/libcore.a
