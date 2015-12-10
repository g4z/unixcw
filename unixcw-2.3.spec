#
# Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#

# Normal descriptive preamble.
Summary:	UnixCW Morse Code Utilities
Name:		unixcw
Version:	2.3
Release:	2
Group:		Applications/Hamradio
Copyright:	GPL
Packager:	Simon Baldwin <simon_baldwin@yahoo.com>
URL:		http://www.geocities.com/simon_baldwin/
Source:		ftp://metalab.unc.edu/pub/Linux/apps/ham/morse/unixcw-2.3.tgz
BuildRoot:	/tmp/unixcw-2.3

%description
The UnixCW utilities add a general purpose CW library to your system, and
a small set of applications based around this library.  These applications
form a Morse code tutor suite, useful for Amateur and Marine radio operators.

# Straightforward preparation for building.
%prep
%setup

# To build, first configure, then make.  Here we set "prefix" to the build
# root, suffixed with "/usr".  So, unlike our natural locations under
# "/usr/local", we build and install the RPM packaged version in "/usr",
# leaving "/usr/local" free for, well, the usual "/usr/local" stuff.
%build
./configure
make prefix="$RPM_BUILD_ROOT/usr"

# Install with "prefix" tweaked in the same way.
%install
make prefix="$RPM_BUILD_ROOT/usr" install

# Clean up our build root.
%clean
rm -rf $RPM_BUILD_ROOT

# Postinstall and postuninstall.
%post
/sbin/ldconfig
%postun
/sbin/ldconfig

# List of packaged files.
%files
/usr/bin/cw
/usr/bin/cwcp
/usr/bin/cwgen
/usr/bin/xcwcp

/usr/include/libcw.h

/usr/lib/libcw.a
/usr/lib/libcw.so
/usr/lib/libcw.so.0
/usr/lib/libcw.so.0.0.0

/usr/man/man1/cw.1.gz
/usr/man/man1/cwcp.1.gz
/usr/man/man1/cwgen.1.gz
/usr/man/man1/xcwcp.1.gz

/usr/man/man3/libcw.3.gz

/usr/man/man7/CW.7.gz
/usr/man/man7/cw.7.gz

/usr/lib/pkgconfig/libcw.pc

%doc COPYING
%doc README
%doc INSTALLING
