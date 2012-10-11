sbin_PROGRAMS		= fsck.reiserfs

man_MANS		= fsck.reiserfs.8
EXTRA_DIST		= fsck.reiserfs.8

fsck_reiserfs_SOURCES	= main.c pass0.c pass1.c pass2.c semantic_check.c     \
			  semantic_rebuild.c pass4.c lost+found.c ubitmap.c   \
			  uobjectid.c ufile.c check_tree.c info.c super.c     \
			  rollback.c stage.c leaf.c utree.c relocate.c fsck.h
fsck_reiserfs_LDADD	= $(top_builddir)/libutil/libutil-static.a            \
			  $(top_builddir)/libreiserfs/libreiserfs-static.a    \
			  $(top_builddir)/libmisc/libmisc-static.a
fsck_reiserfs_LDFLAGS	= @PROGS_LDFLAGS@
fsck_reiserfs_CFLAGS	= @CFLAGS@

INCLUDES		= -I$(top_srcdir)/include

uninstall-reiserfsck:
	if test -f $(DESTDIR)$(sbindir)/reiserfsck; then \
		rm -f $(DESTDIR)$(sbindir)/reiserfsck; \
	fi;

install-reiserfsck:
	if test -f $(DESTDIR)$(sbindir)/fsck.reiserfs; then \
		rm -f $(DESTDIR)$(sbindir)/reiserfsck; \
		ln $(DESTDIR)$(sbindir)/fsck.reiserfs $(DESTDIR)$(sbindir)/reiserfsck; \
	fi;

install-data-local: install-reiserfsck
uninstall-local: uninstall-reiserfsck
