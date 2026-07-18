#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source "$script_dir/lib.sh"

workspace=""
output_file=""

while (($#)); do
	case "$1" in
	--workspace)
		workspace="$2"
		shift 2
		;;
	--output-file)
		output_file="$2"
		shift 2
		;;
	*)
		die "unknown argument: $1"
		;;
	esac
done

[[ -n "$workspace" ]] || die "--workspace is required"

cd "$workspace"
debian/rules clean || true
dpkg-source -b .

source_pkg=$(package_source_name)
full_version=$(package_full_version)
parent_dir=$(dirname -- "$workspace")
dsc="${parent_dir}/${source_pkg}_${full_version}.dsc"
source_tar="${parent_dir}/${source_pkg}_${full_version}.tar.xz"

[[ -f "$dsc" ]] || die "missing generated dsc: $dsc"
[[ -f "$source_tar" ]] || die "missing generated native source tarball: $source_tar"

write_output "$output_file" dsc "$dsc"
write_output "$output_file" source_tar "$source_tar"

