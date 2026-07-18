#!/usr/bin/env bash
set -euo pipefail

artifact_dir=""
output_file=""

while (($#)); do
	case "$1" in
	--artifact-dir)
		artifact_dir="$2"
		shift 2
		;;
	--output-file)
		output_file="$2"
		shift 2
		;;
	*)
		printf 'error: unknown argument: %s\n' "$1" >&2
		exit 2
		;;
	esac
done

[[ -n "$artifact_dir" ]] || { printf 'error: --artifact-dir is required\n' >&2; exit 2; }

debian/rules clean
dpkg-buildpackage -us -uc -S
dpkg-buildpackage -us -uc -b

source_name=$(dpkg-parsechangelog --show-field Source)
version=$(dpkg-parsechangelog --show-field Version)
parent_dir=$(dirname -- "$PWD")
deb="${parent_dir}/${source_name}_${version}_all.deb"
[[ -s "$deb" ]] || {
	printf 'error: missing package: %s\n' "$deb" >&2
	exit 1
}

dpkg-deb --info "$deb"
dpkg-deb --contents "$deb" | grep -q '/usr/src/'
dpkg-deb --contents "$deb" | grep -q '/dkms.conf$'

payload_dir=$(mktemp -d)
trap 'rm -rf "$payload_dir"' EXIT
dpkg-deb --extract "$deb" "$payload_dir"
installed_dkms_conf=$(find "$payload_dir/usr/src" -type f -name dkms.conf -print -quit)
[[ -n "$installed_dkms_conf" ]] || {
	printf 'error: installed DKMS configuration is missing\n' >&2
	exit 1
}
bash -n "$installed_dkms_conf"
if grep -q '#MODULE_VERSION#' "$installed_dkms_conf"; then
	printf 'error: DKMS package version placeholder was not replaced\n' >&2
	exit 1
fi

mkdir -p "$artifact_dir"
find "$parent_dir" -maxdepth 1 -type f -name "${source_name}_${version}*" \
	-exec cp --preserve=mode,timestamps '{}' "$artifact_dir/" \;

if [[ -n "$output_file" ]]; then
	printf 'artifact_dir=%s\n' "$artifact_dir" >>"$output_file"
	printf 'deb=%s/%s\n' "$artifact_dir" "$(basename -- "$deb")" >>"$output_file"
fi
