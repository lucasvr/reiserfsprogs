sbin_PROGRAMS		= tunefs.reiserfs

man_MANS		= tunefs.reiserfs.8
EXTRA_DIST		= tunefs.reiserfs.8

tunefs_reiserfs_SOURCES	= tune.c tune.h
tunefs_reiserfs_LDADD	= $(top_builddir)/libreiserfs/libreiserfs-static.a \
			  $(top_builddir)/libutil/libutil-static.a \
			  $(top_builddir)/libmisc/libmisc-static.a
tunefs_reiserfs_LDFLAGS	= @PROGS_LDFLAGS@
tunefs_reiserfs_CFLAGS	= @CFLAGS@

INCLUDES		= -I$(top_srcdir)/include

uninstall-reiserfstune:
	if test -f $(DESTDIR)$(sbindir)/reiserfstune; then \
		rm -f $(DESTDIR)$(sbindir)/reiserfstune; \
	fi;

install-reiserfstune:
	if test -f $(DESTDIR)$(sbindir)/tunefs.reiserfs; then \
		rm -f $(DESTDIR)$(sbindir)/reiserfstune; \
		ln $(DESTDIR)$(sbindir)/tunefs.reiserfs $(DESTDIR)$(sbindir)/reiserfstune; \
	fi;

install-data-local: install-reiserfstune
uninstall-local: uninstall-reiserfstune
