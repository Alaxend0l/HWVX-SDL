# Sourced by the packaging scripts after setting `target` (a variable: POSIX
# sh can't pass arguments to `.`): sets root, version, work (scratch for
# this target, kept between runs so SDL builds once) and dist (the outputs).
# HWVX_VERSION overrides the version (CI passes the tag); else git describe.
root=$(cd "$(dirname "$0")/../.." && pwd)
if [ -n "${HWVX_VERSION:-}" ]; then
    version=$HWVX_VERSION
else
    version=$(git -C "$root" describe --tags --always --dirty 2>/dev/null || echo dev)
fi
version=$(echo "$version" | sed 's/^v//')
work="$root/build-pkg/$target"
dist="$root/dist"
mkdir -p "$work" "$dist"
