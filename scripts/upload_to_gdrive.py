#!/usr/bin/env python3
# Copyright (c) 2026 EXACT Technology Corporation
"""
Recursively upload a local directory tree to Google Drive, creating any
missing intermediate folders. Auth is the same service-account-JSON +
google-api-python-client pattern used by etc-monitor-fixture/upload_results.py.

Usage:
    upload_to_gdrive.py --key sa.json --parent <PARENT_ID> --remote-path "2.1.0/rc1" --local-dir out/
"""
import argparse
import mimetypes
import sys
from pathlib import Path

from google.oauth2 import service_account
from googleapiclient.discovery import build
from googleapiclient.http import MediaFileUpload

SCOPES = ["https://www.googleapis.com/auth/drive"]
FOLDER_MIME = "application/vnd.google-apps.folder"


def drive_client(key_path):
    creds = service_account.Credentials.from_service_account_file(key_path, scopes=SCOPES)
    return build("drive", "v3", credentials=creds, cache_discovery=False)


def find_child(svc, parent_id, name, mime=None):
    safe_name = name.replace("\\", "\\\\").replace("'", "\\'")
    q = [f"'{parent_id}' in parents", f"name = '{safe_name}'", "trashed = false"]
    if mime:
        q.append(f"mimeType = '{mime}'")
    res = (
        svc.files()
        .list(
            q=" and ".join(q),
            fields="files(id, name, mimeType)",
            supportsAllDrives=True,
            includeItemsFromAllDrives=True,
            pageSize=10,
        )
        .execute()
    )
    files = res.get("files", [])
    return files[0] if files else None


def ensure_folder(svc, parent_id, name):
    existing = find_child(svc, parent_id, name, mime=FOLDER_MIME)
    if existing:
        return existing["id"]
    created = (
        svc.files()
        .create(
            body={"name": name, "mimeType": FOLDER_MIME, "parents": [parent_id]},
            fields="id",
            supportsAllDrives=True,
        )
        .execute()
    )
    return created["id"]


def resolve_path(svc, parent_id, remote_path):
    folder_id = parent_id
    for part in [p for p in remote_path.split("/") if p]:
        folder_id = ensure_folder(svc, folder_id, part)
    return folder_id


def upload_file(svc, parent_id, local_file: Path):
    mime = mimetypes.guess_type(local_file.name)[0] or "application/octet-stream"
    media = MediaFileUpload(str(local_file), mimetype=mime, resumable=True)
    existing = find_child(svc, parent_id, local_file.name)
    if existing:
        svc.files().update(
            fileId=existing["id"], media_body=media, supportsAllDrives=True
        ).execute()
        print(f"updated  {local_file}")
    else:
        svc.files().create(
            body={"name": local_file.name, "parents": [parent_id]},
            media_body=media,
            fields="id",
            supportsAllDrives=True,
        ).execute()
        print(f"uploaded {local_file}")


def upload_tree(svc, parent_id, local_dir: Path):
    for entry in sorted(local_dir.iterdir()):
        if entry.is_dir():
            child_id = ensure_folder(svc, parent_id, entry.name)
            upload_tree(svc, child_id, entry)
        else:
            upload_file(svc, parent_id, entry)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--key", required=True, help="service-account JSON path")
    ap.add_argument("--parent", required=True, help="Drive parent folder ID")
    ap.add_argument("--remote-path", required=True, help='Subpath under parent, e.g. "2.1.0/rc1"')
    ap.add_argument("--local-dir", required=True, help="Local directory to upload")
    args = ap.parse_args()

    local = Path(args.local_dir)
    if not local.is_dir():
        print(f"local-dir does not exist or is not a directory: {local}", file=sys.stderr)
        sys.exit(2)

    svc = drive_client(args.key)
    target_id = resolve_path(svc, args.parent, args.remote_path)
    print(f"target folder id: {target_id}")
    upload_tree(svc, target_id, local)


if __name__ == "__main__":
    main()
