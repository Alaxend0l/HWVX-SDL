#!/bin/sh
# Publishes a GitHub Release from the packages the Build workflow already
# made for a commit, without building anything. Needs the GitHub CLI, logged
# in (gh auth login), and a successful Build run of that commit.
#   packaging/release.sh TAG [--commit COMMIT] [gh release create options...]
#   packaging/release.sh 0.2.0 --prerelease
# The commit is the tag's if the tag exists on GitHub, else COMMIT, else
# HEAD; a new tag is created there. Those builds call themselves
# dev-COMMIT: the files are renamed to TAG, what the programs report stays.
set -eu

die() { echo "release.sh: $*" >&2; exit 1; }

[ $# -ge 1 ] || die "usage: packaging/release.sh TAG [--commit COMMIT] [gh release create options...]"
tag=$1; shift
commit=
if [ "${1:-}" = --commit ]; then
    [ $# -ge 2 ] || die "--commit needs a commit"
    commit=$2; shift 2
fi

# Pushing a v* tag makes the Build workflow publish its own packages to the
# same release, so these would end up in it twice.
case $tag in
    v*) die "tags v* are released by the Build workflow: git tag $tag && git push origin $tag" ;;
esac

command -v gh >/dev/null || die "needs the GitHub CLI (gh): https://cli.github.com"
cd "$(dirname "$0")/.."

if gh release view "$tag" >/dev/null 2>&1; then
    die "release $tag already exists (to replace it: gh release delete $tag)"
fi

[ -z "$commit" ] || commit=$(git rev-parse --verify "$commit^{commit}")
remote=$(git ls-remote origin "refs/tags/$tag" "refs/tags/$tag^{}" | tail -n 1 | cut -f 1)
if [ -n "$remote" ]; then
    [ -z "$commit" ] || [ "$commit" = "$remote" ] || die "tag $tag is at $remote, not $commit"
    commit=$remote
fi
[ -n "$commit" ] || commit=$(git rev-parse HEAD)

# Pull request runs build a merge commit rather than this commit.
run=$(gh run list --workflow build.yml --commit "$commit" --status success \
    --json databaseId,event --jq 'map(select(.event != "pull_request"))[0].databaseId // empty')
[ -n "$run" ] || die "no successful Build run for $commit (not pushed, or still building?)"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir "$tmp/assets"

echo "Downloading the packages of run $run ($commit)"
gh run download "$run" --dir "$tmp/artifacts"

dev=dev-$(echo "$commit" | cut -c 1-7)
find "$tmp/artifacts" -type f | while read -r file; do
    name=$(basename "$file" | sed "s/$dev/$tag/")
    [ ! -e "$tmp/assets/$name" ] || die "two packages are named $name"
    mv "$file" "$tmp/assets/$name"
done
ls -l "$tmp/assets"

# The ordering gives the caller's options the last word.
gh release create "$tag" "$tmp"/assets/* \
    --target "$commit" --title "$tag" --generate-notes "$@"
