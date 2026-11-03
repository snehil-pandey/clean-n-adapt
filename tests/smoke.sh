#!/usr/bin/env bash
# Smoke test for the cna CLI on Linux (GNU coreutils). Builds the CLI, creates a simulated
# Windows-style profile in a temp dir, and asserts the behaviours the documentation promises.
# Usage: tests/smoke.sh        (needs g++; takes ~10 s; touches nothing outside a temp dir)
set -u
HERE="$(cd "$(dirname "$0")/.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
PASS=0; FAIL=0
ok()   { PASS=$((PASS+1)); printf '  PASS  %s\n' "$1"; }
bad()  { FAIL=$((FAIL+1)); printf '  FAIL  %s\n' "$1"; }
check(){ if eval "$2"; then ok "$1"; else bad "$1"; fi; }

echo "building..."
g++ -std=c++17 -Os -Wall -Wextra -o "$T/cna" "$HERE"/src/*.cpp || { echo "build failed"; exit 1; }
C="$T/cna"

U="$T/user"; L="$U/AppData/Local"; A="$U/AppData/Roaming"; PF="$T/PF"
export USERPROFILE="$U" LOCALAPPDATA="$L" APPDATA="$A" TEMP="$L/Temp" PROGRAMDATA="$T/ProgramData" PROGRAMFILES="$PF" SystemRoot="$T/Windows" CNA_HOME="$T/state"
mk() { mkdir -p "$(dirname "$1")"; head -c "$2" /dev/urandom > "$1"; [ -n "${3:-}" ] && touch -d "$3 days ago" "$1"; return 0; }

# caches (some old, one fresh) + files that must survive
mk "$L/Temp/old.tmp" 5000 10;  mk "$L/Temp/fresh.tmp" 3000
mk "$L/Google/Chrome/User Data/Default/Cache/Cache_Data/f1" 100000
mk "$L/Google/Chrome/User Data/Default/Bookmarks" 100
mk "$L/Google/Chrome/User Data/Default/Login Data" 100
# duplicates: 3 equal copies, 1 same-size-different, 1 hard link
mk "$U/Downloads/a.bin" 2097152 30
cp -p "$U/Downloads/a.bin" "$U/Documents-copy.bin" 2>/dev/null; mkdir -p "$U/Documents" "$U/Desktop"
cp -p "$U/Downloads/a.bin" "$U/Documents/a_copy.bin"; cp -p "$U/Downloads/a.bin" "$U/Desktop/a (1).bin"; rm -f "$U/Documents-copy.bin"
cp "$U/Downloads/a.bin" "$U/Documents/almost.bin"; printf 'X' | dd of="$U/Documents/almost.bin" bs=1 seek=100 conv=notrunc 2>/dev/null
ln "$U/Downloads/a.bin" "$U/Documents/hardlink.bin"
# remnants
mk "$L/OldGameLauncher/data.db" 40000 400; mk "$L/RecentThing/a.cfg" 500 2
mk "$L/NotepadStuff/a" 500 500; mk "$L/Microsoft/keep.txt" 5 900
printf 'Notepad++|Notepad++ Team|%s/Notepad++\n' "$PF" > "$T/apps.txt"
# explain
mk "$U/proj/package.json" 50; mk "$U/proj/node_modules/x/i.js" 10

echo "caches"
before=$(find "$L" -type f | wc -l)
"$C" scan --profile quick,browser >/dev/null
check "scan never deletes"                 '[ "$(find "$L" -type f | wc -l)" = "$before" ]'
"$C" clean --profile quick,browser >/dev/null
check "clean without --yes never deletes"  '[ "$(find "$L" -type f | wc -l)" = "$before" ]'
"$C" clean --profile quick,browser --yes >/dev/null
check "old temp file removed"              '[ ! -e "$L/Temp/old.tmp" ]'
check "fresh temp file kept (min age)"     '[ -e "$L/Temp/fresh.tmp" ]'
check "browser cache removed"              '[ ! -e "$L/Google/Chrome/User Data/Default/Cache/Cache_Data/f1" ]'
check "bookmarks + logins untouched"       '[ -e "$L/Google/Chrome/User Data/Default/Bookmarks" ] && [ -e "$L/Google/Chrome/User Data/Default/Login Data" ]'

echo "duplicates"
J=$("$C" dupes "$U/Downloads" "$U/Documents" "$U/Desktop" --min-size 1M --json)
check "one duplicate set found"            '[ "$(grep -o "\"keeper\"" <<<"$J" | wc -l)" = 1 ]'
check "set has 3 files (hard link + near-miss excluded)" '[ "$(grep -o "\.bin\"" <<<"$J" | wc -l)" = 3 ]'
check "wasted = 2 x size"                  'grep -q "\"total_wasted\":4194304" <<<"$J"'
check "--of finds the other copies"        '[ "$("$C" dupes --of "$U/Downloads/a.bin" "$U/Documents" "$U/Desktop" --json | grep -o "\.bin\"" | wc -l)" -ge 3 ]'
"$C" dupes "$U/Downloads" "$U/Documents" "$U/Desktop" --min-size 1M --quarantine --yes >/dev/null
check "extras moved to quarantine"         '[ "$("$C" quarantine list --json | grep -o "\"id\"" | wc -l)" = 2 ]'
ID=$("$C" quarantine list --json | grep -o '"id":"[^"]*"' | head -1 | cut -d'"' -f4)
"$C" quarantine restore "$ID" >/dev/null
check "restore brings a file back"         '[ "$("$C" quarantine list --json | grep -o "\"id\"" | wc -l)" = 1 ]'

echo "remnants"
R=$("$C" remnants --apps-file "$T/apps.txt" --min-score 60 --json)
check "old unowned folder flagged"         'grep -q OldGameLauncher <<<"$R"'
check "recently-used folder not flagged"   '! grep -q RecentThing <<<"$R"'
check "system folder not flagged"          '! grep -q "/Microsoft\"" <<<"$R"'

echo "explain"
check "node_modules is regenerable"        '"$C" explain "$U/proj/node_modules" --no-store --json | grep -q "\"safety\":\"regenerable\""'
check "project folder detected"            '"$C" explain "$U/proj" --no-store --json | grep -q node-project'

echo "safety + cap"
mkdir -p "$U/Documents"; mk "$U/Documents/precious.txt" 100 99
printf 'profiles = custom\nremnants = off\ncustom_rule = ${USERPROFILE}\ncustom_rule = ${USERPROFILE}/Documents\nmin_hours_between_runs = 0\n' > "$CNA_HOME/cna.conf"
"$C" auto run --force >/dev/null 2>&1
check "broad custom rules refused (Documents intact)" '[ -e "$U/Documents/precious.txt" ]'
"$C" clean --profile bogus >/dev/null 2>&1; check "unknown profile -> exit 64" '[ $? = 64 ]'
"$C" scan --mem-cap 8 >/dev/null 2>&1;      check "--mem-cap below 16 MB rejected" '[ $? = 64 ]'
"$C" mem | grep -q "cap: 125.0 MB";         check "default cap is 125 MB" '[ $? = 0 ]'

echo; echo "passed: $PASS   failed: $FAIL"; [ "$FAIL" = 0 ]
