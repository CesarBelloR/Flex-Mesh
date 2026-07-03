#!/usr/bin/env bash
#
# Fail the job if the Claude Code Review did not actually complete.
#
# Reads the following environment variables (set by the workflow from the
# claude-code-action step outputs):
#   STEP_OUTCOME   - outcome of the action step (success/failure/cancelled)
#   EXECUTION_FILE - path to the JSON execution log the action writes
#
# Exits non-zero when the action step errored (e.g. Claude authentication
# failed), no execution log was produced, Claude reported an error
# (is_error / non-success subtype / API error), or any tool call was denied
# (permission_denials_count > 0 means a review action was blocked).

set -euo pipefail

if [ "${STEP_OUTCOME:-}" != "success" ]; then
  echo "::error::Claude review step did not succeed (outcome: ${STEP_OUTCOME:-unset}). The review was not completed (e.g. Claude authentication failed)."
  exit 1
fi

if [ -z "${EXECUTION_FILE:-}" ] || [ ! -f "${EXECUTION_FILE}" ]; then
  echo "::error::Claude review produced no execution log. The review did not run to completion."
  exit 1
fi

# The execution log is a JSON array of SDK messages; the final message with
# type "result" carries the completion status.
result="$(jq -c '[.[] | select(.type == "result")] | last' "${EXECUTION_FILE}")"
if [ -z "${result}" ] || [ "${result}" = "null" ]; then
  echo "::error::No result message in Claude execution log; the review did not complete."
  cat "${EXECUTION_FILE}"
  exit 1
fi
echo "Result: ${result}"

subtype="$(printf '%s' "${result}" | jq -r '.subtype // ""')"
is_error="$(printf '%s' "${result}" | jq -r '.is_error // false')"
api_error="$(printf '%s' "${result}" | jq -r '.api_error_status // "null"')"
denials="$(printf '%s' "${result}" | jq -r '.permission_denials_count // 0')"

failed=0
if [ "${is_error}" = "true" ] || [ "${subtype}" != "success" ]; then
  echo "::error::Claude reported an error (subtype='${subtype}', is_error=${is_error})."
  failed=1
fi
if [ "${api_error}" != "null" ]; then
  echo "::error::Claude hit an API error (api_error_status='${api_error}'); the review did not complete."
  failed=1
fi
if [ "${denials}" -gt 0 ]; then
  echo "::error::Claude review had ${denials} permission denial(s); it could not post its results. Check the job's 'permissions:' block."
  failed=1
fi

if [ "${failed}" -ne 0 ]; then
  exit 1
fi
echo "Claude review completed successfully with no permission denials."
