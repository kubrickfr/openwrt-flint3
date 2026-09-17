#!/bin/sh
# Audit patch application without accepting fuzz or shifted hunks.
# Usage: make target/linux/prepare PATCH="$PWD/scripts/patch-strict.sh" V=s
set -u
log=$(mktemp) || exit 1
trap 'rm -f "$log"' EXIT HUP INT TERM
patch --fuzz=0 "$@" > "$log" 2>&1
ret=$?
cat "$log"
if grep -Eiq 'with fuzz|offset [+-]?[0-9]+ line' "$log"; then
	echo 'ERROR: patch needs an explicit refresh' >&2
	ret=1
fi
exit "$ret"
