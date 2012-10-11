sbin_PROGRAMS	          = resizefs.reiserfs

man_MANS                  = resizefs.reiserfs.8
EXTRA_DIST                = resizefs.reiserfs.8

resizefs_reiserfs_SOURCES = fe.c resize_reiserfs.c do_shrink.c resize.h
resizefs_reiserfs_LDADD	  = $(top_builddir)/libreiserfs/libreiserfs-static.a \
			    $(top_builddir)/libutil/libutil-static.a \
			    $(top_builddir)/libmisc/libmisc-static.a
resizefs_reiserfs_LDFLAGS = @PROGS_LDFLAGS@
resizefs_reiserfs_CFLAGS  = @CFLAGS@

INCLUDES		  = -I$(top_srcdir)/include

uninstall-resize_reiserfs:
	if test -f $(DESTDIR)$(sbindir)/resize_reiserfs; then \
		rm -f $(DESTDIR)$(sbindir)/resize_reiserfs; \
	fi;

install-resize_reiserfs:
	if test -f $(DESTDIR)$(sbindir)/resizefs.reiserfs; then \
		rm -f $(DESTDIR)$(sbindir)/resize_reiserfs; \
		ln $(DESTDIR)$(sbindir)/resizefs.reiserfs $(DESTDIR)$(sbindir)/resize_reiserfs; \
	fi;

install-data-local: install-resize_reiserfs
uninstall-local: uninstall-resize_reiserfs
