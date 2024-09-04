Name: qtcontacts-sqlite-qt5
Version: 0.3.20
Release: 0
Summary: SQLite-based plugin for QtPIM Contacts
License: BSD
URL: https://github.com/sailfishos/qtcontacts-sqlite
Source0: %{name}-%{version}.tar.gz
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5Sql)
BuildRequires: pkgconfig(Qt5DBus)
BuildRequires: pkgconfig(Qt5Contacts) >= 5.2.0
BuildRequires: pkgconfig(mlite5)
Requires: qt5-plugin-sqldriver-sqlite

%description
%{summary}.

%package tests
Summary:    Unit tests for qtcontacts-sqlite-qt5
BuildRequires:  pkgconfig(Qt5Test)
Requires:   blts-tools
Requires:   %{name} = %{version}-%{release}

%description tests
This package contains unit tests for the qtcontacts-sqlite-qt5 library.

%package extensions-devel
Summary:    QtContacts extension headers for qtcontacts-sqlite-qt5
Requires:   %{name} = %{version}-%{release}
Provides:   qtcontacts-sqlite-qt5-extensions > 0.3.19
Obsoletes:  qtcontacts-sqlite-qt5-extensions <= 0.3.19

%description extensions-devel
This package contains extension headers for the qtcontacts-sqlite-qt5 library.

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5 "VERSION=%{version}" "PKGCONFIG_LIB=%{_lib}"
%make_build

%install
%qmake5_install

%files
%license LICENSE.BSD
%{_libdir}/qt5/plugins/contacts/*.so*

%files tests
/opt/tests/qtcontacts-sqlite-qt5/*
%{_libdir}/qtcontacts-sqlite-qt5/libtestdlgg.so

%files extensions-devel
%{_libdir}/pkgconfig/qtcontacts-sqlite-qt5-extensions.pc
%{_includedir}/qtcontacts-sqlite-qt5-extensions/*

