#!/bin/bash
# Summarize a build log: failing files grouped by directory, and the most common error kinds.
L="${1:-$HOME/libertyrecomp-ps4-build/logs/build.log}"
echo "== progress: $(grep -o '^\[[0-9]*/[0-9]*\]' "$L" | tail -1)   failed TUs: $(grep -c '^FAILED' "$L")"
echo "== failed TUs by directory"
grep '^FAILED' "$L" | sed -E 's/^FAILED: (\[code=[0-9]+\] )?//; s| .*||; s|/CMakeFiles/[^/]*\.dir/|  |' \
    | awk '{print $1}' | sort | uniq -c | sort -rn | head -40
echo "== top error messages (normalized)"
grep -h 'error:' "$L" | sed -E 's/^[^ ]+ error: //; s/'"'"'[^'"'"']*'"'"'/X/g' | sort | uniq -c | sort -rn | head -40
echo "== first error per header file (root causes in headers)"
grep -h 'error:' "$L" | grep -oE '^[^:]+\.(h|hpp|inl|inc)' | sort | uniq -c | sort -rn | head -25
