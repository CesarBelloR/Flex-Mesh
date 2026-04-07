#!/usr/bin/env bash
# Validate CBOR diagnostic samples against CDDL schemas.
#
# Prerequisites:
#   gem install cbor-diag cddl
#
# Usage:
#   ./validate.sh          # run all validations
#   ./validate.sh -v       # verbose (show byte sizes)

set -euo pipefail
cd "$(dirname "$0")"

# Add Ruby gem bin directory to PATH (gems installed with --user-install)
GEM_BIN="$(ruby -e 'puts Gem.user_dir' 2>/dev/null)/bin"
[[ -d "$GEM_BIN" ]] && PATH="$GEM_BIN:$PATH"
GEM_BIN="${GEM_HOME:-$HOME/.gem}/bin"
[[ -d "$GEM_BIN" ]] && PATH="$GEM_BIN:$PATH"

DIAG2CBOR="${DIAG2CBOR:-diag2cbor.rb}"
CDDL="${CDDL:-cddl}"

# Check tools
for cmd in "$DIAG2CBOR" "$CDDL"; do
	if ! command -v "$cmd" &>/dev/null; then
		echo "ERROR: '$cmd' not found. Install with: gem install cbor-diag cddl" >&2
		exit 1
	fi
done

VERBOSE=0
[[ "${1:-}" == "-v" ]] && VERBOSE=1

PASS=0
FAIL=0
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

# Each entry: "schema_cddl sample_diag max_bytes description"
TESTS=(
	"storage.cddl samples/storage-simple.diag 120 Storage: simple 9-temp"
	"storage.cddl samples/storage-splitters.diag 120 Storage: chained splitters"
	"storage.cddl samples/storage-chunked-0.diag 120 Storage: chunk 0 of 2"
	"storage.cddl samples/storage-chunked-1.diag 120 Storage: chunk 1 of 2 (no battery)"
	"storage.cddl samples/storage-multi-chunk-0.diag 0 Storage: multi-chunk 0 (splitters+humidity+dual-temp)"
	"storage.cddl samples/storage-multi-chunk-1.diag 0 Storage: multi-chunk 1 (splitters+humidity+dual-temp)"
	"lora-logger-relay.cddl samples/lora-logger-relay.diag 127 LoRa Logger->Relay"
	"lora-relay-portal.cddl samples/lora-relay-portal.diag 0 LoRa Relay->Portal"
	"lora-relay-portal.cddl samples/lora-relay-portal-minimal-skylo.diag 0 LoRa Relay->Portal (minimal)"
)

for test in "${TESTS[@]}"; do
	read -r schema sample max_bytes desc <<< "$test"

	# Convert diagnostic notation to binary CBOR
	cbor_file="$WORK_DIR/$(basename "$sample" .diag).cbor"
	if ! "$DIAG2CBOR" < "$sample" > "$cbor_file" 2>"$WORK_DIR/diag_err"; then
		echo "FAIL  $desc — diag2cbor error: $(cat "$WORK_DIR/diag_err")"
		FAIL=$((FAIL + 1))
		continue
	fi

	byte_size=$(wc -c < "$cbor_file")

	# Resolve .include directives by concatenating referenced files
	resolved_cddl="$WORK_DIR/$(basename "$schema")"
	sed '/^\.include /d' "$schema" > "$resolved_cddl"
	# Append all included files
	while IFS= read -r inc; do
		inc_file=$(echo "$inc" | sed 's/^\.include //')
		cat "$inc_file" >> "$resolved_cddl"
	done < <(grep '^\.include ' "$schema" || true)

	# Validate against CDDL schema
	if ! "$CDDL" "$resolved_cddl" validate "$cbor_file" 2>"$WORK_DIR/cddl_err"; then
		echo "FAIL  $desc — CDDL validation error: $(cat "$WORK_DIR/cddl_err")"
		FAIL=$((FAIL + 1))
		continue
	fi

	# Check byte budget (skip if max_bytes is 0)
	if [[ "$max_bytes" -gt 0 ]] && [[ "$byte_size" -gt "$max_bytes" ]]; then
		echo "FAIL  $desc — ${byte_size} bytes exceeds ${max_bytes}-byte limit"
		FAIL=$((FAIL + 1))
		continue
	fi

	if [[ "$VERBOSE" -eq 1 ]]; then
		echo "PASS  $desc (${byte_size} bytes)"
	else
		echo "PASS  $desc"
	fi
	PASS=$((PASS + 1))
done

echo ""
echo "Results: $PASS passed, $FAIL failed"
[[ "$FAIL" -eq 0 ]] && exit 0 || exit 1
