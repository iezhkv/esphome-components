#!/usr/bin/env python3
"""Integration tests for the access_control REST API.

Runs against a live device. Destructively wipes credentials before/after.
Do NOT run against a production controller.

The device uses a REST-shaped API:
  GET    /credentials              list
  POST   /credentials              create  (body: uid, expires_at, one_time, privileged)
  PATCH  /credentials/<uid>        update  (body: expires_at, one_time, privileged)
  DELETE /credentials/<uid>        delete one
  DELETE /credentials?confirm=...  delete all
  POST   /scan                     RPC (body: uid)

Bodies can be either form-urlencoded or JSON. Status codes:
  200 success; 400 bad input; 403 confirmation required; 404 not found;
  405 method not allowed; 409 already exists; 507 capacity exceeded.

Usage:
    python3 test_rest.py                              # defaults: 192.168.8.16 / door_controller
    python3 test_rest.py --host 10.0.0.5 --id mydoor

Exit 0 on all-green, 1 on any failure.
"""
from __future__ import annotations

import argparse
import json
import sys
import urllib.parse
import urllib.request
from urllib.error import HTTPError


class TestFailure(Exception):
    pass


def request(method: str, url: str, form: dict | None = None, timeout: float = 10.0):
    data = None
    headers = {}
    if form is not None:
        data = urllib.parse.urlencode(form).encode()
        headers["Content-Type"] = "application/x-www-form-urlencoded"
    req = urllib.request.Request(url, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read()
            try:
                return resp.status, json.loads(raw or b"{}")
            except json.JSONDecodeError as e:
                # Surface the actual bytes so we can diagnose truncation /
                # garbage responses from the device.
                raise TestFailure(
                    f"non-JSON response (status {resp.status}, {len(raw)} bytes): "
                    f"{raw!r} -- {e}"
                )
    except HTTPError as e:
        body = e.read()
        try:
            payload = json.loads(body) if body else {}
        except json.JSONDecodeError:
            payload = {"_raw": body.decode(errors="replace")}
        return e.code, payload


class Runner:
    def __init__(self, base: str):
        self.base = base.rstrip("/")
        self.passed = 0
        self.failed: list[str] = []

    def url(self, path: str) -> str:
        return f"{self.base}{path}"

    def check(self, name: str, fn):
        try:
            fn()
            self.passed += 1
            print(f"  PASS  {name}")
        except TestFailure as e:
            self.failed.append(name)
            print(f"  FAIL  {name}: {e}")
        except Exception as e:  # noqa: BLE001
            self.failed.append(name)
            print(f"  ERROR {name}: {type(e).__name__}: {e}")

    def report(self) -> int:
        total = self.passed + len(self.failed)
        print()
        print(f"{self.passed}/{total} passed")
        if self.failed:
            print("Failures:")
            for n in self.failed:
                print(f"  - {n}")
            return 1
        return 0


def expect(cond, msg):
    if not cond:
        raise TestFailure(msg)


def expect_eq(got, want, label="value"):
    if got != want:
        raise TestFailure(f"{label}: got {got!r}, want {want!r}")


def expect_ok(body):
    """Successful op: 200 status assumed; no `error` field in body."""
    if "error" in body:
        raise TestFailure(f"unexpected error in body: {body}")


def expect_error(body, code):
    if body.get("error") != code:
        raise TestFailure(f"expected error={code!r}, got body={body}")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="192.168.8.16")
    p.add_argument("--id", default="door_controller",
                   help="component id (becomes the REST path segment)")
    p.add_argument("--scheme", default="http")
    args = p.parse_args()

    base = f"{args.scheme}://{args.host}/access_control/{args.id}"
    r = Runner(base)

    print(f"Testing against {base}")
    print()

    print("Pre-clean:")
    r.check("wipe all", lambda: _wipe(r))

    print("\nEmpty-state reads:")
    r.check("list returns empty", lambda: _list_empty(r))

    print("\nSingle-credential CRUD:")
    r.check("add new", lambda: _add(r, "TEST0001"))
    r.check("add duplicate -> ALREADY_EXISTS", lambda: _add_dup(r, "TEST0001"))
    r.check("add empty uid -> INVALID_UID", lambda: _add_invalid(r, ""))
    r.check("add overlong uid -> INVALID_UID", lambda: _add_invalid(r, "X" * 32))
    r.check("list shows added", lambda: _list_has(r, "TEST0001"))
    r.check("update", lambda: _update(r, "TEST0001", expires_at=1767225600, one_time=1, privileged=0))
    r.check("update reflected in list", lambda: _list_field(r, "TEST0001", "expires_at", 1767225600))
    r.check("update nonexistent -> NOT_FOUND", lambda: _update_missing(r))
    r.check("delete", lambda: _delete(r, "TEST0001"))
    r.check("delete nonexistent -> NOT_FOUND", lambda: _delete_missing(r))

    print("\nScan:")
    # Seed one known credential so we can test grant. Test cred is "GRANT001".
    r.check("seed credential for scan", lambda: _add(r, "GRANT001"))
    r.check("scan unknown uid -> NOT_FOUND", lambda: _scan(r, "NOPE_NEVER", granted=False, code="NOT_FOUND"))
    r.check("scan known uid granted (relay will click)", lambda: _scan(r, "GRANT001", granted=True, code=""))

    print("\nDelete all:")
    r.check("delete-all without confirm -> CONFIRMATION_REQUIRED",
            lambda: _delete_all_no_confirm(r))
    r.check("delete-all with confirm", lambda: _wipe(r))
    r.check("list empty after wipe", lambda: _list_empty(r))

    print("\nRouting:")
    r.check("unknown subpath -> UNKNOWN_ROUTE", lambda: _unknown_route(r))

    return r.report()


# ---- individual checks ----


def _wipe(r):
    # DELETE on the collection with confirm param = delete all.
    confirm = urllib.parse.quote("delete all credentials")
    code, body = request("DELETE", r.url(f"/credentials?confirm={confirm}"))
    expect_eq(code, 200)
    expect_ok(body)
    expect_eq(body.get("count"), 0, "count")


def _list_empty(r):
    code, body = request("GET", r.url("/credentials"))
    expect_eq(code, 200)
    expect_ok(body)
    expect_eq(body.get("count"), 0, "count")
    expect_eq(body.get("credentials"), [], "credentials")


def _add(r, uid):
    code, body = request("POST", r.url("/credentials"),
                         form={"uid": uid, "expires_at": 0, "one_time": 0, "privileged": 0})
    expect_eq(code, 200)
    expect_ok(body)
    expect_eq(body.get("uid"), uid)


def _add_dup(r, uid):
    code, body = request("POST", r.url("/credentials"),
                         form={"uid": uid, "expires_at": 0})
    expect_eq(code, 409)
    expect_error(body, "ALREADY_EXISTS")


def _add_invalid(r, uid):
    code, body = request("POST", r.url("/credentials"),
                         form={"uid": uid, "expires_at": 0})
    expect_eq(code, 400)
    expect_error(body, "INVALID_UID")


def _list_has(r, uid):
    code, body = request("GET", r.url("/credentials"))
    expect_eq(code, 200)
    expect_ok(body)
    uids = [c["uid"] for c in body.get("credentials", [])]
    expect(uid in uids, f"{uid} not in {uids}")


def _list_field(r, uid, field, want):
    code, body = request("GET", r.url("/credentials"))
    expect_eq(code, 200)
    expect_ok(body)
    match = next((c for c in body["credentials"] if c["uid"] == uid), None)
    expect(match is not None, f"{uid} not found in list")
    expect_eq(match[field], want, field)


def _list_count(r, want):
    code, body = request("GET", r.url("/credentials"))
    expect_eq(code, 200)
    expect_ok(body)
    expect_eq(body.get("count"), want, "count")


def _update(r, uid, **fields):
    code, body = request("PATCH", r.url(f"/credentials/{urllib.parse.quote(uid)}"), form=fields)
    expect_eq(code, 200)
    expect_ok(body)
    expect_eq(body.get("uid"), uid)


def _update_missing(r):
    code, body = request("PATCH", r.url("/credentials/NOPE_NEVER"),
                         form={"expires_at": 0})
    expect_eq(code, 404)
    expect_error(body, "NOT_FOUND")


def _delete(r, uid):
    code, body = request("DELETE", r.url(f"/credentials/{urllib.parse.quote(uid)}"))
    expect_eq(code, 200)
    expect_ok(body)
    expect_eq(body.get("uid"), uid)


def _delete_missing(r):
    code, body = request("DELETE", r.url("/credentials/NOPE_NEVER"))
    expect_eq(code, 404)
    expect_error(body, "NOT_FOUND")


def _scan(r, uid, granted, code=None):
    status, body = request("POST", r.url("/scan"), form={"uid": uid})
    expect_eq(status, 200)
    expect_ok(body)
    expect_eq(body.get("uid"), uid)
    expect_eq(body.get("granted"), granted, "granted")
    if code is not None:
        expect_eq(body.get("code"), code, "code")


def _delete_all_no_confirm(r):
    code, body = request("DELETE", r.url("/credentials"))
    expect_eq(code, 403)
    expect_error(body, "CONFIRMATION_REQUIRED")


def _unknown_route(r):
    code, body = request("GET", r.url("/nope_unknown"))
    expect_eq(code, 404)
    expect_error(body, "NOT_FOUND")


if __name__ == "__main__":
    sys.exit(main())
