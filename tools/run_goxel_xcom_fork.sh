#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_PATH="${GOXEL_XCOM_APP_PATH:-$HOME/Library/Developer/Xcode/DerivedData/Build/Products/Release/Goxel XCom Fork.app}"
BUILD=0
OPEN_ARGS=()

usage() {
    cat <<USAGE
Usage: $(basename "$0") [--build] [--new-instance]

Options:
  --build         Build the Release app before launching it.
  --new-instance Launch a separate app instance instead of activating one.
USAGE
}

while (($#)); do
    case "$1" in
        --build|--rebuild)
            BUILD=1
            ;;
        --new-instance)
            OPEN_ARGS=(-n)
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if [[ ! -d "$APP_PATH" ]]; then
    BUILD=1
fi

if ((BUILD)); then
    xcodebuild \
        -project "$ROOT_DIR/osx/goxel/goxel.xcodeproj" \
        -scheme goxel \
        -configuration Release \
        CODE_SIGN_IDENTITY=- \
        CODE_SIGNING_REQUIRED=NO \
        CODE_SIGNING_ALLOWED=NO \
        build
fi

if [[ ! -d "$APP_PATH" ]]; then
    echo "Could not find built app at: $APP_PATH" >&2
    exit 1
fi

open "${OPEN_ARGS[@]}" "$APP_PATH"
