#!/usr/bin/env bash
set -euo pipefail

die() {
	printf 'error: %s\n' "$*" >&2
	exit 1
}

repo_root() {
	git rev-parse --show-toplevel
}

package_source_name() {
	dpkg-parsechangelog --show-field Source
}

package_full_version() {
	dpkg-parsechangelog --show-field Version
}

package_upstream_version() {
	package_full_version | sed 's/-[^-]*$//'
}

package_distribution() {
	dpkg-parsechangelog --show-field Distribution
}

normalized_sbuild_suite() {
	local suite="$1"
	if [[ -z "$suite" || "$suite" == "UNRELEASED" || "$suite" == "unreleased" ]]; then
		printf 'trixie\n'
	else
		printf '%s\n' "$suite"
	fi
}

gbp_upstream_tag() {
	local version="$1"
	printf 'upstream/%s\n' "${version//\~/_}"
}

write_output() {
	local output_file="$1"
	local key="$2"
	local value="$3"

	if [[ -z "$output_file" ]]; then
		return 0
	fi

	printf '%s=%s\n' "$key" "$value" >>"$output_file"
}

ensure_git_identity() {
	local name=""
	local email=""

	name=${GIT_AUTHOR_NAME:-${GIT_COMMITTER_NAME:-}}
	email=${GIT_AUTHOR_EMAIL:-${GIT_COMMITTER_EMAIL:-}}

	if [[ -z "$name" ]]; then
		name=$(git config --get user.name 2>/dev/null || true)
	fi

	if [[ -z "$email" ]]; then
		email=$(git config --get user.email 2>/dev/null || true)
	fi

	if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
		name=${name:-${CI_GIT_NAME:-github-actions[bot]}}
		email=${email:-${CI_GIT_EMAIL:-41898282+github-actions[bot]@users.noreply.github.com}}
	else
		name=${name:-${CI_GIT_NAME:-recipe-ci}}
		email=${email:-${CI_GIT_EMAIL:-recipe-ci@local.invalid}}
	fi

	export GIT_AUTHOR_NAME="${GIT_AUTHOR_NAME:-$name}"
	export GIT_AUTHOR_EMAIL="${GIT_AUTHOR_EMAIL:-$email}"
	export GIT_COMMITTER_NAME="${GIT_COMMITTER_NAME:-$name}"
	export GIT_COMMITTER_EMAIL="${GIT_COMMITTER_EMAIL:-$email}"
}

ensure_local_branch_from_remote() {
	local remote="$1"
	local branch="$2"
	local ref="refs/remotes/${remote}/${branch}"

	if git show-ref --verify --quiet "$ref"; then
		git branch -f "$branch" "$ref" >/dev/null
		return 0
	fi

	return 1
}

ensure_empty_branch() {
	local branch="$1"
	local empty_tree="4b825dc642cb6eb9a060e54bf8d69288fbee4904"
	local commit

	commit=$(printf 'Initial %s branch\n' "$branch" | git commit-tree "$empty_tree")
	git update-ref "refs/heads/${branch}" "$commit"
}

copy_if_exists() {
	local output_dir="$1"
	shift

	for file in "$@"; do
		if [[ -f "$file" ]]; then
			copy_artifact_file "$output_dir" "$file"
		fi
	done
}

safe_artifact_name() {
	local name="$1"

	name=${name//$'\r'/_}
	name=${name//$'\n'/_}
	printf '%s\n' "$name" | sed 's/["*:<>|?]/_/g'
}

copy_artifact_file() {
	local output_dir="$1"
	local file="$2"
	local dest_name=""
	local dest_path=""

	dest_name=$(safe_artifact_name "$(basename -- "$file")")
	dest_path="${output_dir}/${dest_name}"
	cp -L --preserve=mode,timestamps "$file" "$dest_path"
}
