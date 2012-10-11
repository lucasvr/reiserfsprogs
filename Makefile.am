SUBDIRS		= include libmisc libreiserfs libutil utils

EXTRA_DIST	= AUTHORS COPYING NEWS README CREDITS \
		  reiserfsprogs.spec reiserfsprogs.spec.in depcomp
#		  libreiserfs.m4

aclocaldir 	= $(datadir)/aclocal
#aclocal_DATA	= libreiserfs.m4

define all-sources
	( find ./ -name '*.[ch]' | grep -v SCCS | grep -v "Attic" | grep -v "reiserfsprogs" )
endef

TAGS:
	$(all-sources) | etags -

tags:
	$(all-sources) | xargs -- ctags -w -o TAGS
