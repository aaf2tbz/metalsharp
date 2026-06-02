#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <app-or-dmg> [<app-or-dmg> ...]" >&2
  exit 2
fi

for target in "$@"; do
  if [ ! -e "$target" ]; then
    echo "Notarization target not found: $target" >&2
    exit 1
  fi

  case "$target" in
    *.app)
      codesign --verify --deep --strict --verbose=2 "$target"
      xcrun stapler validate "$target"
      spctl -a -vvv --type execute "$target"
      ;;
    *.dmg)
      spctl -a -vvv --type open --context context:primary-signature "$target"
      ;;
    *)
      echo "Unsupported notarization verification target: $target" >&2
      exit 2
      ;;
  esac
done
