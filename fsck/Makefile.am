sbin_PROGRAMS = reiserfsck

reiserfsck_SOURCES = main.c pass0.c pass1.c pass2.c semantic_check.c \
semantic_rebuild.c pass4.c lost+found.c ubitmap.c uobjectid.c ustree.c \
ufile.c check_tree.c info.c super.c fsck.h

man_MANS = reiserfsck.8
EXTRA_DIST = $(man_MANS)

reiserfsck_LDADD = $(top_srcdir)/lib/libmisc.a $(top_srcdir)/reiserfscore/libcore.a

