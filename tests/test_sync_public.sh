#!/usr/bin/env bash
# Host tests for scripts/sync-public.sh. Uses temp dirs and local git repos.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="$ROOT/scripts/sync-public.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

PASS=0
FAIL=0

ok()   { echo "PASS $1"; PASS=$((PASS + 1)); }
bad()  { echo "FAIL $1"; FAIL=$((FAIL + 1)); }

assert_file()    { if [ -e "$1" ]; then ok "$2"; else bad "$2 (missing $1)"; fi; }
assert_no_file() { if [ ! -e "$1" ]; then ok "$2"; else bad "$2 (unexpected $1)"; fi; }
assert_eq()      { if [ "$1" = "$2" ]; then ok "$3"; else bad "$3 (got '$1', want '$2')"; fi; }
assert_contains(){ if grep -qF "$2" "$1" 2>/dev/null; then ok "$3"; else bad "$3 (no '$2' in $1)"; fi; }

make_source() {
    local d="$1"
    mkdir -p "$d/src" "$d/docs/screenshots" "$d/scripts" "$d/public/docs" "$d/public/scripts"
    printf 'cmake_minimum_required(VERSION 3.10)\n' > "$d/CMakeLists.txt"
    printf 'int main(void){return 0;}\n' > "$d/src/main.c"
    printf 'png\n' > "$d/docs/screenshots/snake.png"
    cat > "$d/scripts/public-manifest.txt" <<'EOF'
CMakeLists.txt
src
docs/screenshots
scripts/test.sh
EOF
    printf '#!/bin/sh\nexit 0\n' > "$d/scripts/test.sh"
    chmod +x "$d/scripts/test.sh"
    cp "$ROOT/public/README.md" "$d/public/README.md"
    cp "$ROOT/public/docs/index.md" "$d/public/docs/index.md"
    printf '#!/bin/sh\nexit 0\n' > "$d/public/scripts/test.sh"
    chmod +x "$d/public/scripts/test.sh"
}

make_public() {
    local d="$1" remote="$2"
    git init --bare -q "$remote"
    git init -q -b main "$d"
    mkdir -p "$d/.github/workflows" "$d/docs"
    printf '# old readme\n' > "$d/README.md"
    printf '# old index\n' > "$d/docs/index.md"
    printf '# old doc\n' > "$d/docs/OLD.md"
    printf 'name: build\n' > "$d/.github/workflows/build.yml"
    printf 'stale\n' > "$d/stale.txt"
    git -C "$d" add -A
    git -C "$d" -c user.name=test -c user.email=test@example.com \
        commit -q -m "init"
    git -C "$d" remote add origin "$remote"
    git -C "$d" push -q -u origin main
}

run_sync() {
    local src="$1" pub="$2"
    shift 2
    SOURCE_DIR="$src" PUBLIC_REPO_DIR="$pub" bash "$SCRIPT" "$@"
}

# --- case 1: happy path -------------------------------------------------------
SRC="$TMP/src"
PUB="$TMP/pub"
REMOTE="$TMP/remote.git"
make_source "$SRC"
make_public "$PUB" "$REMOTE"

if run_sync "$SRC" "$PUB" --yes --no-push >"$TMP/out1" 2>&1; then
    ok "happy: exit 0"
else
    bad "happy: exit nonzero"; cat "$TMP/out1"
fi
assert_file "$PUB/src/main.c" "happy: source copied"
assert_file "$PUB/CMakeLists.txt" "happy: root file copied"
assert_no_file "$PUB/docs/OLD.md" "happy: stale doc removed"
assert_no_file "$PUB/stale.txt" "happy: stale root removed"
assert_file "$PUB/.github/workflows/build.yml" "happy: .github preserved"
assert_eq "$(git -C "$PUB" status --porcelain)" "" "happy: clean tree"
assert_contains "$PUB/docs/index.md" "main" "happy: index uses main"
assert_contains "$PUB/README.md" "PSP Playground" "happy: public README copied"

# --- case 2: idempotence ------------------------------------------------------
BEFORE="$(git -C "$PUB" rev-list --count HEAD)"
if run_sync "$SRC" "$PUB" --yes --no-push >"$TMP/out2" 2>&1; then
    ok "idempotent: exit 0"
else
    bad "idempotent: exit nonzero"; cat "$TMP/out2"
fi
AFTER="$(git -C "$PUB" rev-list --count HEAD)"
assert_eq "$AFTER" "$BEFORE" "idempotent: no new commit"
assert_contains "$TMP/out2" "nothing to sync" "idempotent: message"

# --- case 3: missing manifest path -------------------------------------------
cp -a "$SRC" "$TMP/src-missing"
rm -f "$TMP/src-missing/CMakeLists.txt"
if run_sync "$TMP/src-missing" "$PUB" --yes --no-push >"$TMP/out3" 2>&1; then
    bad "missing path: should fail"
else
    ok "missing path: fails"
fi
assert_contains "$TMP/out3" "CMakeLists.txt" "missing path: names the path"

# --- case 4: dirty clone ------------------------------------------------------
touch "$PUB/dirty.txt"
if run_sync "$SRC" "$PUB" --yes --no-push >"$TMP/out4" 2>&1; then
    bad "dirty clone: should fail"
else
    ok "dirty clone: fails"
fi
assert_contains "$TMP/out4" "dirty" "dirty clone: message"
rm -f "$PUB/dirty.txt"

# --- case 5: tag --------------------------------------------------------------
if run_sync "$SRC" "$PUB" --tag v1.2.3 --yes --no-push >"$TMP/out5" 2>&1; then
    ok "tag: exit 0"
else
    bad "tag: exit nonzero"; cat "$TMP/out5"
fi
if git -C "$PUB" rev-parse -q --verify refs/tags/v1.2.3 >/dev/null; then
    ok "tag: v1.2.3 exists"
else
    bad "tag: v1.2.3 missing"
fi
assert_contains "$PUB/docs/index.md" "v1.2.3" "tag: index uses v1.2.3"

# --- case 6: existing tag -----------------------------------------------------
if run_sync "$SRC" "$PUB" --tag v1.2.3 --yes --no-push >"$TMP/out6" 2>&1; then
    bad "existing tag: should fail"
else
    ok "existing tag: fails"
fi
assert_contains "$TMP/out6" "v1.2.3" "existing tag: names the tag"

# --- case 7: path escape ------------------------------------------------------
cp -a "$SRC" "$TMP/src-escape"
printf '../evil\n' >> "$TMP/src-escape/scripts/public-manifest.txt"
if run_sync "$TMP/src-escape" "$PUB" --yes --no-push >"$TMP/out7" 2>&1; then
    bad "path escape: should fail"
else
    ok "path escape: fails"
fi

# --- case 8: private path guard ----------------------------------------------
cp -a "$SRC" "$TMP/src-private"
printf 'private\n' > "$TMP/src-private/AGENTS.md"
printf 'AGENTS.md\n' >> "$TMP/src-private/scripts/public-manifest.txt"
if run_sync "$TMP/src-private" "$PUB" --yes --no-push >"$TMP/out8" 2>&1; then
    bad "private path: should fail"
else
    ok "private path: fails"
fi
assert_contains "$TMP/out8" "private" "private path: message"

# --- summary ------------------------------------------------------------------
echo
echo "sync-public tests: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
