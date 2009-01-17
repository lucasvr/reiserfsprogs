sbin_PROGRAMS = mkreiserfs

mkreiserfs_SOURCES = mkreiserfs.c
man_MANS = mkreiserfs.8
EXTRA_DIST = $(man_MANS)

LDADD = $(top_srcdir)/reiserfscore/libcore.a $(top_srcdir)/lib/libmisc.a

install-exec-hook:
	$(LN_S) mkreiserfs $(DESTDIR)$(sbindir)/mkfs.reiserfs
