#
#    agent-outage - Agent that sends alerts when device does not communicate.
#
#    Copyright (C) 2014 - 2015 Eaton                                        
#                                                                           
#    This program is free software; you can redistribute it and/or modify   
#    it under the terms of the GNU General Public License as published by   
#    the Free Software Foundation; either version 2 of the License, or      
#    (at your option) any later version.                                    
#                                                                           
#    This program is distributed in the hope that it will be useful,        
#    but WITHOUT ANY WARRANTY; without even the implied warranty of         
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          
#    GNU General Public License for more details.                           
#                                                                           
#    You should have received a copy of the GNU General Public License along
#    with this program; if not, write to the Free Software Foundation, Inc.,
#    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.            
#

Name:           agent-outage
Version:        0.1.0
Release:        1
Summary:        agent that sends alerts when device does not communicate.
License:        GPL-2.0+
URL:            https://eaton.com/
Source0:        %{name}-%{version}.tar.gz
Group:          System/Libraries
BuildRequires:  automake
BuildRequires:  autoconf
BuildRequires:  libtool
BuildRequires:  pkg-config
BuildRequires:  systemd-devel
BuildRequires:  zeromq-devel
BuildRequires:  czmq-devel
BuildRequires:  malamute-devel
BuildRequires:  libbiosproto-devel
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%description
agent-outage agent that sends alerts when device does not communicate..

%package -n libagent_outage0
Group:          System/Libraries
Summary:        agent that sends alerts when device does not communicate.

%description -n libagent_outage0
agent-outage agent that sends alerts when device does not communicate..
This package contains shared library.

%post -n libagent_outage0 -p /sbin/ldconfig
%postun -n libagent_outage0 -p /sbin/ldconfig

%files -n libagent_outage0
%defattr(-,root,root)
%{_libdir}/libagent_outage.so.*

%package devel
Summary:        agent that sends alerts when device does not communicate.
Group:          System/Libraries
Requires:       libagent_outage0 = %{version}
Requires:       zeromq-devel
Requires:       czmq-devel
Requires:       malamute-devel
Requires:       libbiosproto-devel

%description devel
agent-outage agent that sends alerts when device does not communicate..
This package contains development files.

%files devel
%defattr(-,root,root)
%{_includedir}/*
%{_libdir}/libagent_outage.so
%{_libdir}/pkgconfig/libagent_outage.pc

%prep
%setup -q

%build
sh autogen.sh
%{configure} --with-systemd-units
make %{_smp_mflags}

%install
make install DESTDIR=%{buildroot} %{?_smp_mflags}

# remove static libraries
find %{buildroot} -name '*.a' | xargs rm -f
find %{buildroot} -name '*.la' | xargs rm -f

%files
%defattr(-,root,root)
%{_bindir}/bios-agent-outage
%{_prefix}/lib/systemd/system/bios-agent-outage*.service


%changelog
