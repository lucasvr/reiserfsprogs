libreiserfs_sources		= badblock.c    fix_node.c     leaf.c               stat.c	 \
				  bitmap.c      hash.c         leaf_balance.c       buffer.c     \
				  internal.c    libreiserfs.c  internal_balance.c   super.c	 \
				  direntry.c    node.c         tree.c	            extent.c	 \
				  journal.c     objmap.c       tree_balance.c       filesystem.c \
				  key.c         prints.c

#lib_LIBRARIES			= libreiserfs.la
noinst_LIBRARIES		= libreiserfs-static.a

#libreiserfs_la_LIBADD		= $(top_builddir)/libmisc/libmisc-static.la
#libreiserfs_la_SOURCES		= $(libreiserfs_sources)
# libreiserfs_la_LDFLAGS	= -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
#				  -release $(LT_RELEASE)
#libreiserfs_la_CFLAGS		= @CFLAGS@

libreiserfs_static_a_LIBADD	= $(top_builddir)/libmisc/libmisc-static.a 
libreiserfs_static_a_SOURCES	= $(libreiserfs_sources)
#libreiserfs_static_a_LDFLAGS	= -static
libreiserfs_static_a_CFLAGS	= @CFLAGS@

INCLUDES			= -I$(top_srcdir)/include
