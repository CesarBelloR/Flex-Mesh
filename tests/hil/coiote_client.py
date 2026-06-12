"""Minimal AVSystem Coiote DM v3 REST client for HIL tests.

Self-contained (depends only on ``requests``) so the firmware HIL suite does not
need the separate ``etc-tools/coiote_api`` repo on the path. Implements just the
slice of the API the relay-command E2E test needs: OAuth, a configure task that
executes an LwM2M resource and reads another, task-report polling, a cached
data-model read, and task deletion.

API shape confirmed against ``etc-tools/coiote_api/doc/v3.json`` and the auth
flow in ``etc-tools/coiote_api/network_change_detector.py``.
"""

import datetime
import time
import urllib.parse

import requests


class CoioteError(RuntimeError):
    """Raised when a Coiote API call fails or a task ends in error."""


class CoioteClient:
    """Thin Coiote DM v3 client with transparent token refresh."""

    def __init__(self, api_host, username, password, timeout=30):
        # api_host already includes the ``/api`` suffix, e.g.
        # https://us.iot.avsystem.cloud:8087/api
        self.api_host = api_host.rstrip("/")
        self.v3 = f"{self.api_host}/coiotedm/v3"
        self._username = username
        self._password = password
        self._timeout = timeout
        self._session = requests.Session()
        self._token = None

    # -- auth ---------------------------------------------------------------

    def authenticate(self):
        """Obtain a bearer token via the OAuth password grant."""
        resp = self._session.post(
            f"{self.api_host}/auth/oauth_password",
            data={
                "grant_type": "password",
                "username": self._username,
                "password": self._password,
            },
            headers={"Content-Type": "application/x-www-form-urlencoded"},
            timeout=self._timeout,
        )
        if not resp.ok:
            raise CoioteError(f"Auth failed ({resp.status_code}): {resp.text}")
        token = resp.json().get("access_token")
        if not token:
            raise CoioteError(f"No access_token in auth response: {resp.text}")
        self._token = token

    def _request(self, method, url, **kwargs):
        """Issue a request, authenticating lazily and refreshing once on 401."""
        if self._token is None:
            self.authenticate()
        kwargs.setdefault("timeout", self._timeout)
        headers = kwargs.pop("headers", {})
        headers["Authorization"] = f"Bearer {self._token}"
        headers.setdefault("Accept", "application/json")
        resp = self._session.request(method, url, headers=headers, **kwargs)
        if resp.status_code == 401:
            # Token expired (Coiote tokens live only a few minutes); refresh once.
            self.authenticate()
            headers["Authorization"] = f"Bearer {self._token}"
            resp = self._session.request(method, url, headers=headers, **kwargs)
        return resp

    # -- device -------------------------------------------------------------

    def device_last_contact(self, device_id):
        """Return the device's lastContactTime as an aware datetime, or None."""
        url = f"{self.v3}/devices/{urllib.parse.quote(device_id, safe='')}"
        resp = self._request("GET", url)
        if resp.status_code == 404:
            return None
        if not resp.ok:
            raise CoioteError(f"GET device failed ({resp.status_code}): {resp.text}")
        ts = resp.json().get("lastContactTime")
        return _parse_iso(ts) if ts else None

    # -- tasks --------------------------------------------------------------

    def configure_execute_read(self, device_id, command_key, argument,
                               response_key, name="hil-relay-cmd"):
        """Create a configure task that executes ``command_key`` with ``argument``
        then reads ``response_key``. Returns the new task id.

        ``executeImmediately`` is intentionally left at its default (false) so
        the task is queued and delivered on the device's next registration. The
        DUT is a queue-mode (PSM/eDRX) cellular device that sleeps between
        check-ins; an immediate downlink would just time out.
        """
        body = {
            "taskDefinition": {
                "operations": [
                    {
                        "execute": {
                            "key": command_key,
                            "argumentList": [{"digit": "0", "argument": argument}],
                        }
                    },
                    {"read": {"key": response_key}},
                ],
                "name": name,
                "taskExecutionLog": "All",
            }
        }
        url = f"{self.v3}/tasks/configure/{urllib.parse.quote(device_id, safe='')}"
        resp = self._request("POST", url, json=body)
        if not resp.ok:
            raise CoioteError(f"Configure task failed ({resp.status_code}): {resp.text}")
        # The endpoint returns the task id as a bare JSON string (sometimes an
        # object with an "id" field, depending on tenant config).
        data = resp.json()
        return data["id"] if isinstance(data, dict) else data

    def get_task_report(self, task_id, device_id):
        """Return the TaskReportDTO dict for a task on a device (or None)."""
        url = (f"{self.v3}/taskReports/{urllib.parse.quote(str(task_id), safe='')}"
               f"/{urllib.parse.quote(device_id, safe='')}")
        resp = self._request("GET", url)
        if resp.status_code == 404:
            return None
        if not resp.ok:
            raise CoioteError(f"GET taskReport failed ({resp.status_code}): {resp.text}")
        return resp.json()

    def wait_for_task(self, task_id, device_id, timeout, poll_interval=5):
        """Poll the task report until it reaches a terminal status.

        Returns the final report dict on success; raises CoioteError on an
        error status or timeout. ``InProgress``/``NotStarted`` (queue-mode
        latency) and a missing report are treated as still-pending.
        """
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            report = self.get_task_report(task_id, device_id)
            if report is not None:
                last = report
                status = report.get("status")
                if status in ("Success", "Warning"):
                    return report
                if status in ("Error", "ErrorRetryable"):
                    raise CoioteError(
                        f"Task {task_id} ended in {status}: {report.get('summary')}")
            time.sleep(poll_interval)
        raise CoioteError(
            f"Task {task_id} did not finish within {timeout}s "
            f"(last report: {last})")

    def delete_task(self, task_id):
        """Best-effort delete of a configure task (they are persistent state)."""
        url = f"{self.v3}/tasks/{urllib.parse.quote(str(task_id), safe='')}"
        try:
            self._request("DELETE", url)
        except requests.RequestException:
            pass

    # -- data model ---------------------------------------------------------

    def read_cached(self, device_id, key):
        """Read a single cached data-model parameter.

        Returns ``(value, update_time)`` where ``update_time`` is an aware
        datetime (or None). Raises if the key is absent.
        """
        url = f"{self.v3}/cachedDataModels/{urllib.parse.quote(device_id, safe='')}"
        resp = self._request("GET", url, params={"parameters": key})
        if not resp.ok:
            raise CoioteError(
                f"GET cachedDataModels failed ({resp.status_code}): {resp.text}")
        entries = resp.json()
        if not isinstance(entries, list):
            raise CoioteError(f"Unexpected cachedDataModels payload: {entries!r}")
        match = next((e for e in entries if e.get("name") == key), None)
        if match is None:
            raise CoioteError(f"Resource '{key}' not present in cached data model")
        update_time = _parse_iso(match.get("updateTime")) if match.get("updateTime") else None
        return match.get("value"), update_time


def _parse_iso(value):
    """Parse an ISO-8601 timestamp (with optional trailing Z) to aware UTC."""
    if value.endswith("Z"):
        value = value[:-1] + "+00:00"
    dt = datetime.datetime.fromisoformat(value)
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=datetime.timezone.utc)
    return dt
