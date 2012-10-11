sbin_PROGRAMS		= mkfs.reiserfs

man_MANS		= mkfs.reiserfs.8
EXTRA_DIST		= mkfs.reiserfs.8

mkfs_reiserfs_SOURCES	= mkreiserfs.c
mkfs_reiserfs_LDADD	= $(top_builddir)/libreiserfs/libreiserfs-static.a \
			  $(top_builddir)/libutil/libutil-static.a         \
			  $(top_builddir)/libmisc/libmisc-static.a
mkfs_reiserfs_LDFLAGS	= @PROGS_LDFLAGS@
mkfs_reiserfs_CFLAGS	= @CFLAGS@

INCLUDES		= -I$(top_srcdir)/include

uninstall-mkreiserfs:
	if test -f $(DESTDIR)$(sbindir)/mkreiserfs; then \
		rm -f $(DESTDIR)$(sbindir)/mkreiserfs; \
	fi;

install-mkreiserfs:
	if test -f $(DESTDIR)$(sbindir)/mkfs.reiserfs; then \
		rm -f $(DESTDIR)$(sbindir)/mkreiserfs; \
		ln $(DESTDIR)$(sbindir)/mkfs.reiserfs $(DESTDIR)$(sbindir)/mkreiserfs; \
	fi;

install-data-local: install-mkreiserfs
uninstall-local: uninstall-mkreiserfs
