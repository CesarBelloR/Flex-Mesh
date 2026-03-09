#!/usr/bin/env python3
"""Claude PR Review - AI-powered inline code review using Claude Opus."""

import json
import os
import re
import subprocess
import sys

import anthropic

REVIEW_PREFIX = "\U0001f916 **Claude Review**"


def gh_api(endpoint, method="GET", data=None):
    """Call GitHub API via gh CLI. Returns parsed JSON, or None on error/empty."""
    cmd = ["gh", "api", endpoint]
    if method != "GET":
        cmd.extend(["-X", method])
    if data:
        cmd.extend(["--input", "-"])
        result = subprocess.run(
            cmd, capture_output=True, text=True, input=json.dumps(data)
        )
    else:
        result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"GitHub API error: {result.stderr}", file=sys.stderr)
        return None
    if not result.stdout.strip():
        return None
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError:
        print(f"Failed to parse GitHub API response as JSON", file=sys.stderr)
        return None


def gh_api_paginated(endpoint):
    """Call GitHub API with pagination. Returns a flat list of items."""
    result = subprocess.run(
        ["gh", "api", endpoint, "--paginate", "--jq", ".[]"],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        print(f"GitHub API error: {result.stderr}", file=sys.stderr)
        return []
    items = []
    decoder = json.JSONDecoder()
    raw = result.stdout.strip()
    if not raw:
        return []
    pos = 0
    while pos < len(raw):
        try:
            obj, end = decoder.raw_decode(raw, pos)
        except json.JSONDecodeError:
            break
        items.append(obj)
        pos = end
        while pos < len(raw) and raw[pos] in " \t\r\n":
            pos += 1
    return items


def get_pr_files():
    """Get changed files with patches from the PR."""
    repo = os.environ["GITHUB_REPOSITORY"]
    pr_number = os.environ["PR_NUMBER"]
    return gh_api_paginated(f"/repos/{repo}/pulls/{pr_number}/files")


def parse_valid_lines(patch):
    """Parse a unified diff patch to extract valid new-side line numbers."""
    if not patch:
        return set()

    valid_lines = set()
    current_line = 0

    for line in patch.split("\n"):
        m = re.match(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@", line)
        if m:
            current_line = int(m.group(1))
            continue

        if line.startswith("\\ "):
            continue  # "\ No newline at end of file" marker
        elif line.startswith("+"):
            valid_lines.add(current_line)
            current_line += 1
        elif line.startswith("-"):
            pass  # removed lines don't increment new-file counter
        else:
            if current_line > 0:
                valid_lines.add(current_line)
                current_line += 1

    return valid_lines


def build_prompt(file_sections):
    """Build the review prompt for Claude."""
    all_patches = "\n\n".join(file_sections)
    return f"""You are a senior code reviewer performing a pull request review.
Analyze the following diff and identify concrete issues.

For each issue, provide the exact filename, the line number in the NEW version
of the file (right side of the diff - lines marked with + or unchanged context
lines), and a clear review comment.

Focus on:
- Bugs, logic errors, off-by-one errors
- Security vulnerabilities (injection, buffer overflow, etc.)
- Resource leaks (memory, file handles, locks)
- Race conditions and concurrency issues
- Null/undefined reference risks
- Missing error handling for operations that can fail
- Performance problems (unnecessary allocations, O(n^2) where O(n) is possible)
- API misuse or incorrect function arguments

Do NOT flag:
- Style preferences or formatting (a linter handles that)
- Missing comments or documentation
- Intentional refactoring or renames
- TODOs or minor suggestions

Be precise. Only report real issues you are confident about.
False positives waste developer time.

Respond with ONLY a JSON array (no markdown fences, no explanation).
Each element:
{{"path": "file/path.c", "line": 42, "body": "**Bug:** Description of the issue.\\n\\nSuggested fix: ..."}}

If there are no issues, respond with: []

Each file's diff is wrapped in <file> tags with path and status attributes.

<diff>
{all_patches}
</diff>"""


def review_with_claude(files):
    """Send changed files to Claude for review and get inline comments."""
    client = anthropic.Anthropic()

    file_sections = []
    valid_lines_map = {}

    for f in files:
        filename = f["filename"]
        patch = f.get("patch", "")
        status = f.get("status", "")

        if not patch or status == "removed":
            continue

        valid_lines_map[filename] = parse_valid_lines(patch)
        file_sections.append(
            f"<file path=\"{filename}\" status=\"{status}\">\n"
            f"```diff\n{patch}\n```\n</file>"
        )

    if not file_sections:
        print("No reviewable changes found.")
        return []

    response = client.messages.create(
        model="claude-opus-4-6",
        max_tokens=16384,
        thinking={"type": "adaptive"},
        messages=[{"role": "user", "content": build_prompt(file_sections)}],
    )

    # Extract the text block, skipping any thinking blocks
    text = ""
    for block in response.content:
        if block.type == "text":
            text = block.text.strip()
            break

    if not text:
        print("No text response from Claude", file=sys.stderr)
        return []

    # Handle markdown code fences if Claude wraps the JSON
    json_match = re.search(r"```(?:json)?\s*(\[.*?])\s*```", text, re.DOTALL)
    if json_match:
        text = json_match.group(1)

    try:
        comments = json.loads(text)
    except json.JSONDecodeError:
        print(
            f"Failed to parse Claude response as JSON:\n{text}",
            file=sys.stderr,
        )
        return []

    # Validate: ensure each comment targets a line present in the diff
    validated = []
    for c in comments:
        path = c.get("path", "")
        line = c.get("line", 0)
        body = c.get("body", "")

        if not path or not line or not body:
            continue

        valid = valid_lines_map.get(path, set())
        if line not in valid:
            if valid:
                nearest = min(valid, key=lambda v: abs(v - line))
                if abs(nearest - line) <= 3:
                    line = nearest
                else:
                    print(
                        f"Skipping comment on {path}:{line} - not in diff",
                        file=sys.stderr,
                    )
                    continue
            else:
                continue

        validated.append(
            {
                "path": path,
                "line": line,
                "side": "RIGHT",
                "body": f"{REVIEW_PREFIX}\n\n{body}",
            }
        )

    return validated


def collect_previous_review_comment_ids():
    """Collect node IDs of inline comments from previous Claude reviews."""
    repo = os.environ["GITHUB_REPOSITORY"]
    pr_number = os.environ["PR_NUMBER"]

    comments = gh_api_paginated(f"/repos/{repo}/pulls/{pr_number}/comments")
    ids = []
    for comment in comments:
        body = comment.get("body", "")
        node_id = comment.get("node_id", "")
        if body.startswith(REVIEW_PREFIX) and node_id:
            ids.append(node_id)
    return ids


def minimize_comments(node_ids):
    """Minimize (hide) comments using GitHub's GraphQL API."""
    query = """
        mutation($id: ID!) {
            minimizeComment(input: {subjectId: $id, classifier: OUTDATED}) {
                minimizedComment { isMinimized }
            }
        }
    """
    for node_id in node_ids:
        result = subprocess.run(
            ["gh", "api", "graphql",
             "-f", f"query={query}",
             "-F", f"id={node_id}"],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            print(
                f"Failed to minimize comment {node_id}: {result.stderr}",
                file=sys.stderr,
            )


def submit_review(comments):
    """Submit a PR review with inline comments via GitHub API."""
    repo = os.environ["GITHUB_REPOSITORY"]
    pr_number = os.environ["PR_NUMBER"]
    head_sha = os.environ["PR_HEAD_SHA"]

    # Collect previous review comment IDs BEFORE submitting the new review,
    # so we don't accidentally minimize the comments we're about to post.
    previous_ids = collect_previous_review_comment_ids()

    if not comments:
        print("No issues found - skipping review submission.")
        review = {
            "commit_id": head_sha,
            "body": f"{REVIEW_PREFIX} - No issues found. Looks good!",
            "event": "COMMENT",
        }
        result = gh_api(
            f"/repos/{repo}/pulls/{pr_number}/reviews",
            method="POST",
            data=review,
        )
        if result:
            minimize_comments(previous_ids)
        return

    review = {
        "commit_id": head_sha,
        "body": f"{REVIEW_PREFIX} - Found {len(comments)} issue(s).",
        "event": "COMMENT",
        "comments": comments,
    }

    result = gh_api(
        f"/repos/{repo}/pulls/{pr_number}/reviews",
        method="POST",
        data=review,
    )
    if result:
        print(f"Review submitted with {len(comments)} comment(s).")
        minimize_comments(previous_ids)
    else:
        print("Failed to submit review.", file=sys.stderr)
        sys.exit(1)


def main():
    required_vars = [
        "ANTHROPIC_API_KEY", "GITHUB_TOKEN", "PR_NUMBER",
        "GITHUB_REPOSITORY", "PR_HEAD_SHA",
    ]
    missing = [v for v in required_vars if not os.environ.get(v)]
    if missing:
        print(
            f"Missing required environment variables: {', '.join(missing)}",
            file=sys.stderr,
        )
        sys.exit(1)

    files = get_pr_files()
    if not files:
        print("No changed files found (API error or empty PR).", file=sys.stderr)
        sys.exit(1)
    print(f"Reviewing {len(files)} changed file(s)...")

    comments = review_with_claude(files)
    print(f"Claude found {len(comments)} issue(s).")

    submit_review(comments)


if __name__ == "__main__":
    main()
