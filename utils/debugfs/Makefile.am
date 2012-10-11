sbin_PROGRAMS		 = debugfs.reiserfs

man_MANS		 = debugfs.reiserfs.8
EXTRA_DIST		 = debugfs.reiserfs.8

debugfs_reiserfs_SOURCES = debugreiserfs.c pack.c unpack.c stat.c corruption.c \
			   scan.c recover.c debugreiserfs.h
debugfs_reiserfs_LDADD	 = $(top_builddir)/libreiserfs/libreiserfs-static.a \
			   $(top_builddir)/libutil/libutil-static.a         \
			   $(top_builddir)/libmisc/libmisc-static.a
debugfs_reiserfs_LDFLAGS = @PROGS_LDFLAGS@
debugfs_reiserfs_CFLAGS	 = @CFLAGS@

INCLUDES		 = -I$(top_srcdir)/include

uninstall-debugreiserfs:
	if test -f $(DESTDIR)$(sbindir)/debugreiserfs; then \
		rm -f $(DESTDIR)$(sbindir)/debugreiserfs; \
	fi;

install-debugreiserfs:
	if test -f $(DESTDIR)$(sbindir)/debugfs.reiserfs; then \
		rm -f $(DESTDIR)$(sbindir)/debugreiserfs; \
		ln $(DESTDIR)$(sbindir)/debugfs.reiserfs $(DESTDIR)$(sbindir)/debugreiserfs; \
	fi;

install-data-local: install-debugreiserfs
uninstall-local: uninstall-debugreiserfs
