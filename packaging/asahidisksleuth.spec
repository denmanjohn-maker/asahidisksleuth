Name:           asahidisksleuth
Version:        0.1.0
Release:        1%{?dist}
Summary:        Honest disk usage analysis for Linux

License:        MIT
URL:            https://github.com/denmanjohn-maker/asahidisksleuth
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  extra-cmake-modules
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  qt6-qtsvg-devel
BuildRequires:  kf6-kirigami-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-kcrash-devel

Requires:       qt6-qtbase
Requires:       qt6-qtdeclarative
Requires:       kf6-kirigami
Requires:       gvfs

%description
Disk Sleuth shows where your disk space really went, with honest sizes
read straight from the kernel. Logical is what files claim; physical is
what the disk actually holds. Hardlinks are charged once, sparse and
compressed files report real savings, and unreadable folders are flagged
rather than silently skipped.

Includes an interactive Kirigami GUI (sunburst, tree, inspector, trash)
and a full CLI: scan, top, info, overview, snapshots.

%prep
%autosetup

%build
%cmake -G Ninja
%cmake_build

%install
%cmake_install

%check
%ctest

%files
%license LICENSE
%doc README.md
%{_bindir}/asahidisksleuth
%{_bindir}/asahidisksleuth-app
%{_datadir}/applications/asahidisksleuth.desktop
%{_datadir}/metainfo/org.asahidisksleuth.app.metainfo.xml

%changelog
* Tue Sep 08 2026 John Denman - 0.1.0-1
- Initial release: CLI + Kirigami GUI, btrfs snapshot awareness
