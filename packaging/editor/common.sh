# Sourced by the editor's packaging scripts after packaging/common.sh: sets
# game_version (the game's, v* tags), ed_version (the editor's: $HWVX_EDITOR_VERSION,
# else the last editor-v* tag, else git describe), ed (scratch) and xcrate (the
# hwvx-xfile crate that builds hwvx-x: $HWVX_X_CRATE, else the superproject
# above this checkout; empty when there is none and $HWVX_X names the program).
game_version=$version
if [ -n "${HWVX_EDITOR_VERSION:-}" ]; then
    ed_version=$HWVX_EDITOR_VERSION
else
    ed_version=$(git -C "$root" describe --tags --match 'editor-v*' 2>/dev/null ||
        git -C "$root" describe --tags --always --dirty 2>/dev/null || echo dev)
fi
ed_version=$(echo "$ed_version" | sed 's/^editor-v//; s/^v//')
ed="$work/editor"
mkdir -p "$ed/bin"
xcrate=
if [ -z "${HWVX_X:-}" ]; then
    if [ -n "${HWVX_X_CRATE:-}" ]; then
        xcrate=$HWVX_X_CRATE
    else
        xcrate=$root
        while [ "$xcrate" != / ] && ! grep -q '^name = "hwvx-xfile"' "$xcrate/Cargo.toml" 2>/dev/null; do
            xcrate=$(dirname "$xcrate")
        done
    fi
    grep -q '^name = "hwvx-xfile"' "$xcrate/Cargo.toml" 2>/dev/null ||
        { echo "no hwvx-x: set HWVX_X (the program) or HWVX_X_CRATE (the hwvx-xfile crate)" >&2; exit 1; }
fi
