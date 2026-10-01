#!/bin/sh
# The Arduino IDE cannot include files from outside the sketch folder, so
# ship_hud/ carries copies of the headers in lib/. lib/ is the source of truth.
#
#   tools/sync_headers.sh           copy lib/ -> ship_hud/
#   tools/sync_headers.sh --check   fail if the copies have drifted (used by CI)
set -eu
cd "$(dirname "$0")/.."

status=0
for src in lib/hud_proto/*.h lib/hud_widgets/*.h; do
  dst="ship_hud/$(basename "$src")"
  if [ "${1:-}" = "--check" ]; then
    if ! cmp -s "$src" "$dst"; then
      echo "out of sync: $dst (run tools/sync_headers.sh)"
      status=1
    fi
  else
    cp "$src" "$dst"
  fi
done
exit $status
