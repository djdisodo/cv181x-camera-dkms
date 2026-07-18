#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source "$script_dir/lib.sh"

artifact_dir=""

while (($#)); do
	case "$1" in
	--artifact-dir)
		artifact_dir="$2"
		shift 2
		;;
	*)
		die "unknown argument: $1"
		;;
	esac
done

[[ -d "$artifact_dir" ]] || die "invalid --artifact-dir"

source_pkg=$(package_source_name)
full_version=$(package_full_version)
deb="${artifact_dir}/${source_pkg}_${full_version}_all.deb"
[[ -s "$deb" ]] || die "missing package: $deb"

dpkg-deb --info "$deb"
dpkg-deb --contents "$deb" | grep -q '/usr/src/'
dpkg-deb --contents "$deb" | grep -q '/dkms.conf$'

payload_dir=$(mktemp -d)
trap 'rm -rf "$payload_dir"' EXIT
dpkg-deb --extract "$deb" "$payload_dir"
installed_dkms_conf=$(find "$payload_dir/usr/src" -type f -name dkms.conf -print -quit)
[[ -n "$installed_dkms_conf" ]] || die "installed DKMS configuration is missing"

bash -n "$installed_dkms_conf"
if grep -q '#MODULE_VERSION#' "$installed_dkms_conf"; then
	die "DKMS package version placeholder was not replaced"
fi
if ! grep -Eq '^MAKE\[[0-9]+\]="make -C \$\{kernel_source_dir\} M=' "$installed_dkms_conf"; then
	die "DKMS build does not invoke Kbuild directly"
fi
