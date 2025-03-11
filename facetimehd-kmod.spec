# buildforkernels macro hint: when you build a new version or a new release
# that contains bugfixes or other improvements then you must disable the
# "buildforkernels newest" macro for just that build; immediately after
# queuing that build enable the macro again for subsequent builds; that way
# a new akmod package will only get build when a new one is actually needed
%global buildforkernels current

Name:       facetimehd-kmod
Version:    0.1
Release:    1%{?dist}
Summary:    Kernel module for Facetime HD (Broadcom 1570) PCIe webcam
Group:      System Environment/Kernel
License:    Redistributable, no modification permitted
URL:        https://github.com/patjak/bcwc_pcie
Source0:    https://github.com/patjak/bcwc_pcie/archive/master.tar.gz

BuildRequires:  %{_bindir}/kmodtool
ExclusiveArch:  i686 x86_64

%{!?kernels:BuildRequires: buildsys-build-rpmfusion-kerneldevpkgs-%{?buildforkernels:%{buildforkernels}}%{!?buildforkernels:current}-%{_target_cpu} }

# kmodtool does its magic here
%{expand:%(kmodtool --target %{_target_cpu} --repo rpmfusion --kmodname %{name} %{?buildforkernels:--%{buildforkernels}} %{?kernels:--for-kernels "%{?kernels}"} 2>/dev/null) }

%description
Linux driver for the Facetime HD (Broadcom 1570) PCIe webcam found in recent
Macbooks. This driver is experimental! Use at your own risk!

%prep
# error out if there was something wrong with kmodtool
%{?kmodtool_check}

# print kmodtool output for debugging purposes:
kmodtool --target %{_target_cpu}  --repo rpmfusion --kmodname %{name} %{?buildforkernels:--%{buildforkernels}} %{?kernels:--for-kernels "%{?kernels}"} 2>/dev/null

%setup -q -c -T
mkdir %{name}-%{version}-src
pushd %{name}-%{version}-src
tar xzf %{SOURCE0}
popd

for kernel_version in %{?kernel_versions} ; do
 cp -a %{name}-%{version}-src _kmod_build_${kernel_version%%___*}
done

%build
for kernel_version in %{?kernel_versions}; do
 pushd _kmod_build_${kernel_version%%___*}/bcwc_pcie-master
 make -C ${kernel_version##*___} M=`pwd` modules
 popd
done

%install
rm -rf ${RPM_BUILD_ROOT}

for kernel_version in %{?kernel_versions}; do
 pushd _kmod_build_${kernel_version%%___*}/bcwc_pcie-master
 mkdir -p ${RPM_BUILD_ROOT}%{kmodinstdir_prefix}${kernel_version%%___*}%{kmodinstdir_postfix}
 install -m 0755 *.ko ${RPM_BUILD_ROOT}%{kmodinstdir_prefix}${kernel_version%%___*}%{kmodinstdir_postfix}
 mkdir -p ${RPM_BUILD_ROOT}%{_libexecdir}/%{name}
 install -m 0755 firmware/extract-firmware.sh ${RPM_BUILD_ROOT}%{_libexecdir}/%{name}
 install -m 0755 firmware/Makefile ${RPM_BUILD_ROOT}%{_libexecdir}/%{name}
 mkdir -p ${RPM_BUILD_ROOT}/usr/lib/firmware/facetimehd
 touch ${RPM_BUILD_ROOT}/usr/lib/firmware/facetimehd/firmware.bin
 popd
done

chmod 0755 $RPM_BUILD_ROOT%{kmodinstdir_prefix}*%{kmodinstdir_postfix}/* || :
%{?akmod_install}

%clean
rm -rf $RPM_BUILD_ROOT

# We need to define a userland package as described at
# http://rpmfusion.org/Packaging/KernelModules/Kmods2#userland_package

%package common
Summary:    Firmware extraction tools for facetimhd driver
Provides:   %{name}-firmware = %{?epoch:%{epoch}:}%{version}
Requires:   %{_bindir}/xzcat
Requires:   %{_bindir}/curl

# Because of https://bugzilla.redhat.com/show_bug.cgi?id=1318084
Requires:   cpio >= 2.12

%description common
Upon install this package will attempt to download and install
firmware from Apple! The binary file is not distributed with the RPM
due to licensing issues but is owned by the package so it can be removed
cleanly.


%files common
%defattr(-,root,root,-)
%{_libexecdir}/%{name}/extract-firmware.sh
%{_libexecdir}/%{name}/Makefile
/usr/lib/firmware/facetimehd/firmware.bin

# NOTE: this scriptlet is intentionally left verbose so the user can
# see if download & extract goes wrong
%post common
pushd %{_libexecdir}/%{name}
make
make install
popd

%changelog

* Thu Jul 20 2017 Alexander Todorov <atodorov@redhat.com> - 0.1-1
- Initial build
