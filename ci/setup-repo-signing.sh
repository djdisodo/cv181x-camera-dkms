#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source "$script_dir/lib.sh"

env_file=""
gpg_home=""
public_key_dir=""

while (($#)); do
	case "$1" in
		--env-file)
			env_file="$2"
			shift 2
			;;
		--home)
			gpg_home="$2"
			shift 2
			;;
		--public-key-dir)
			public_key_dir="$2"
			shift 2
			;;
		*)
			die "unknown argument: $1"
			;;
	esac
done

[[ -n "${DEB_REPO_SIGNING_PRIVATE_KEY:-}" ]] || die "DEB_REPO_SIGNING_PRIVATE_KEY is required"

if [[ -z "$gpg_home" ]]; then
	gpg_home=$(mktemp -d)
fi

mkdir -p "$gpg_home"
chmod 700 "$gpg_home"

export GNUPGHOME="$gpg_home"
umask 077

cat >"$GNUPGHOME/gpg-agent.conf" <<'EOF'
allow-loopback-pinentry
EOF
gpgconf --kill gpg-agent >/dev/null 2>&1 || true

key_file="$GNUPGHOME/repo-signing-key.asc"
if printf '%s\n' "$DEB_REPO_SIGNING_PRIVATE_KEY" | grep -q 'BEGIN PGP PRIVATE KEY BLOCK'; then
	printf '%s\n' "$DEB_REPO_SIGNING_PRIVATE_KEY" >"$key_file"
else
	printf '%s' "$DEB_REPO_SIGNING_PRIVATE_KEY" | base64 --decode >"$key_file" \
		|| die "DEB_REPO_SIGNING_PRIVATE_KEY is neither ASCII-armored nor valid base64"
fi

gpg --batch --import "$key_file" >/dev/null
rm -f "$key_file"

sign_key="${DEB_S3_SIGN_KEY:-}"
if [[ -n "$sign_key" ]]; then
	gpg --batch --list-secret-keys "$sign_key" >/dev/null 2>&1 \
		|| die "requested signing key '$sign_key' is not present after import"
else
	sign_key=$(gpg --batch --list-secret-keys --with-colons | awk -F: '/^fpr:/ { print $10; exit }')
	[[ -n "$sign_key" ]] || die "could not determine signing key fingerprint"
fi

gpg_options="--batch --no-tty --pinentry-mode loopback"
if [[ -n "${DEB_REPO_SIGNING_PASSPHRASE:-}" ]]; then
	passphrase_file="$GNUPGHOME/repo-signing.passphrase"
	printf '%s' "$DEB_REPO_SIGNING_PASSPHRASE" >"$passphrase_file"
	gpg_options+=" --passphrase-file $passphrase_file"
fi

if [[ -n "$public_key_dir" ]]; then
	mkdir -p "$public_key_dir"
	gpg --batch --armor --export "$sign_key" >"$public_key_dir/repository-signing-key.asc"
	gpg --batch --export "$sign_key" >"$public_key_dir/repository-signing-key.gpg"
fi

write_output "$env_file" GNUPGHOME "$GNUPGHOME"
write_output "$env_file" DEB_S3_SIGN_KEY "$sign_key"
write_output "$env_file" DEB_S3_GPG_OPTIONS "$gpg_options"

printf 'Loaded repository signing key %s into %s\n' "$sign_key" "$GNUPGHOME" >&2
