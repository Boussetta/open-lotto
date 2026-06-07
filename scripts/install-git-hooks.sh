#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
hooks_dir="$repo_root/.githooks"
pre_commit_hook="$hooks_dir/pre-commit"

if [[ ! -d "$hooks_dir" ]]; then
	printf 'Expected hooks directory not found: %s\n' "$hooks_dir" >&2
	exit 1
fi

if [[ ! -f "$pre_commit_hook" ]]; then
	printf 'Expected pre-commit hook not found: %s\n' "$pre_commit_hook" >&2
	exit 1
fi

chmod +x "$pre_commit_hook"

git -C "$repo_root" config core.hooksPath .githooks
printf 'Git hooks path set to %s\n' "$hooks_dir"
printf 'Pre-commit hook is installed and executable.\n'
