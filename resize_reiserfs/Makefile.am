sbin_PROGRAMS = resize_reiserfs

resize_reiserfs_SOURCES = fe.c resize_reiserfs.c do_shrink.c resize.h
man_MANS = resize_reiserfs.8
EXTRA_DIST = $(man_MANS)

LDADD = $(top_srcdir)/lib/libmisc.a $(top_srcdir)/reiserfscore/libcore.a

