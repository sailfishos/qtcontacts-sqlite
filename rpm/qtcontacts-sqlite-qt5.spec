Name: qtcontacts-sqlite-qt5
Version: 0.3.4
Release: 0
Summary: SQLite-based plugin for QtPIM Contacts
License: BSD
URL: https://git.sailfishos.org/mer-core/qtcontacts-sqlite
Source0: %{name}-%{version}.tar.gz
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5Sql)
BuildRequires: pkgconfig(Qt5DBus)
BuildRequires: pkgconfig(Qt5Contacts) >= 5.2.0
BuildRequires: pkgconfig(mlite5)
Requires: qt5-plugin-sqldriver-sqlite

%description
%{summary}.

%files
%defattr(-,root,root,-)
%{_libdir}/qt5/plugins/contacts/*.so*

%package tests
Summary:    Unit tests for qtcontacts-sqlite-qt5
BuildRequires:  pkgconfig(Qt5Test)
Requires:   blts-tools
Requires:   %{name} = %{version}-%{release}

%description tests
This package contains unit tests for the qtcontacts-sqlite-qt5 library.

%files tests
%defattr(-,root,root,-)
/opt/tests/qtcontacts-sqlite-qt5/*
%{_libdir}/qtcontacts-sqlite-qt5/libtestdlgg.so

%package extensions
Summary:    QtContacts extension headers for qtcontacts-sqlite-qt5
Requires:   %{name} = %{version}-%{release}

%description extensions
This package contains extension headers for the qtcontacts-sqlite-qt5 library.

%files extensions
%defattr(-,root,root,-)
%{_libdir}/pkgconfig/qtcontacts-sqlite-qt5-extensions.pc
%{_includedir}/qtcontacts-sqlite-qt5-extensions/*


%prep
%setup -q -n %{name}-%{version}

%build
%qmake5 "VERSION=%{version}" "PKGCONFIG_LIB=%{_lib}"
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%qmake5_install

