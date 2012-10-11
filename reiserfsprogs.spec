Name: reiserfsprogs
Version: 3.6.20
Release: 1
Summary: Utilities for reiserfs filesystems
License: GPL
Group: System Environment/Base
URL: http://www.namesys.com/
Source: reiserfsprogs-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%description
The reiserfsprogs package contains utilities for manipulating
reiserfs filesystems.

%prep
%setup -q -n reiserfsprogs-3.6.20

%build
  export CFLAGS="$RPM_OPT_FLAGS" CXXFLAGS="$RPM_OPT_FLAGS"
  find . -name "config.cache" |xargs rm -f
  ./configure --sbindir=%{_sbindir} --mandir=%{_mandir}

%install
  rm -rf $RPM_BUILD_ROOT
  %{__make} DESTDIR=$RPM_BUILD_ROOT install

# __os_install_post is normally executed after \% install disable it
%define ___build_post %{nil}
# explicitly call it now, so manpages get compressed, exec's stripped etc.
%{?__os_install_post}
%define __os_install_post %{nil}

# now we have all the files execpt for docs, but their owner is unimportant
cd $RPM_BUILD_ROOT

%clean
  rm -rf $RPM_BUILD_ROOT

%post
  rm -rf $RPM_BUILD_ROOT
  CONFIG=/usr/src/linux/.config

if [ -f $CONFIG ] ; then
  source $CONFIG
fi

if [ -z $CONFIG_REISERFS_FS ] ; then
  echo -e "\nIn $CONFIG , you probably have to set:"
  if [ "$CONFIG_EXPERIMENTAL" != "y" ] ; then
    echo -e 'CONFIG_EXPERIMENTAL=y'
  fi
  echo -e 'CONFIG_REISERFS_FS=y\n  or'
  echo -e 'CONFIG_REISERFS_FS=m'
  echo -e 'and recompile and reboot your kernel if you cannot use the\nreiserfsprogs utilities'
fi

%files
%defattr(-,root,root)
%doc AUTHORS COPYING CREDITS INSTALL NEWS README
%{_sbindir}/*
%{_mandir}/man8/*.gz
