# SPDX-License-Identifier: GPL-3.0-only
"""Run the native client through a planning deferral and an independent server edit."""

import argparse
from contextlib import ExitStack
from datetime import timedelta
from hashlib import sha256
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
from tempfile import TemporaryDirectory
from threading import Event, Thread
from unittest.mock import patch
from uuid import uuid4

from fastapi import Response
from fastapi.testclient import TestClient
import uvicorn

from test_order_reconnection import ok, wait_for
from app import work_orders
from app.compatibility import VERSION
from app.main import create_app
from test_visit_sync_integrity import report
from test_work_orders import account
from test_workflow import upload, xlsx


EMAIL = "pending-visit@example.invalid"
OBSERVATION = "Ligação revista na Âncora / inspected on site."


def exercise(executable, port, scenario):
    admin_password = "Admin9!" + secrets.token_hex(12)
    field_password = "Field9!" + secrets.token_hex(12)
    os.environ.update(ADMIN_EMAIL="admin@lem.pt", ADMIN_PASSWORD=admin_password)
    offline = Event()
    order_started = Event()
    initial_time = work_orders.lisbon_now()
    planned_time = initial_time + timedelta(days=1)
    start = planned_time.date().isoformat()
    attempts = []

    def lisbon_clock():
        return planned_time if order_started.is_set() else initial_time

    with TemporaryDirectory(prefix="lem-order-start-") as directory, socket.socket() as listener:
        state = Path(directory)
        listener.bind(("127.0.0.1", port))
        app = create_app("sqlite:///" + str(state / "fixture.db"))

        @app.middleware("http")
        async def field_connection(request, next_handler):
            control = request.headers.get("X-Lem-Test-Control") == "1"
            if not control and offline.is_set():
                return Response(status_code=503)
            response = await next_handler(request)
            if not control and request.method == "PUT" and request.url.path.startswith("/api/reports/"):
                attempts.append((request.url.path, response.status_code))
            return response

        headers = {"X-Lem-Test-Control": "1", "X-Lem-Client-Version": VERSION, "X-Lem-Work-Orders": "1"}
        with ExitStack() as resources:
            resources.callback(app.state.factory.kw["bind"].dispose)
            resources.enter_context(patch.object(work_orders, "lisbon_now", lisbon_clock))
            api = resources.enter_context(TestClient(app, headers=headers))
            admin_login = ok(api.post("/api/login", json={"email": "admin@lem.pt", "password": admin_password}))
            admin = {"Authorization": "Bearer " + admin_login["token"]}
            tech = ok(api.post("/api/users", headers=admin, json={
                "name": "Técnica Ângela", "email": EMAIL, "role": "tech", "password": field_password,
            }))
            token = ok(api.post("/api/login", json={"email": EMAIL, "password": field_password}))["token"]
            field = {"Authorization": "Bearer " + token}
            next_id, _ = account(api, admin, "another-crew")
            contract = ok(api.post("/api/contracts", headers=admin, json={
                "company": "Âncora Field", "year": initial_time.year, "semester": 2, "kind": "MP",
            }))
            ok(upload(api, admin, contract["id"], True, xlsx(("Ação-Â North",))))
            station, = ok(api.get("/api/stations", headers=admin))
            target = str(uuid4())
            ok(api.put(f"/api/interventions/{target}", headers=admin, json={
                "kind": "commissioning", "station_id": station["id"], "technician": tech["id"],
            }))

            runner = uvicorn.Server(uvicorn.Config(app, log_level="warning", lifespan="off"))
            worker = Thread(target=runner.run, kwargs={"sockets": [listener]}, daemon=True)
            worker.start()
            process = None
            try:
                wait_for(lambda: runner.started, "private API startup")
                process = subprocess.Popen([
                    str(executable), "--order-start-retry", f"http://127.0.0.1:{port}", str(state), target, scenario,
                ], env={**os.environ, "QT_QPA_PLATFORM": "offscreen", "QT_FORCE_STDERR_LOGGING": "1",
                        "LEM_ORDER_TEST_PASSWORD": field_password, "LEM_TEST_CLIENT_VERSION": VERSION})
                print(f"{scenario}: native PID {process.pid}, fixture PID {os.getpid()}, port {port}.", flush=True)
                wait_for(lambda: (state / "opened").exists(), "intervention loaded", process)
                offline.set()
                (state / "offline").touch()
                saved = state / "saved"
                wait_for(lambda: saved.exists() and saved.stat().st_size >= 36, "local draft saved", process)
                rid = json.loads(saved.read_bytes())["id"]
                endpoint = f"/api/reports/{rid}"
                assert ok(api.get(endpoint + "/exists", headers=field))["exists"] is False
                order_id = str(uuid4())
                itinerary = f"/api/work-orders/{order_id}"
                order = ok(api.put(itinerary, headers=admin, json={
                    "id": order_id, "revision": 0, "start": start, "due": start,
                    "technicians": [next_id], "targets": [target],
                }))
                assert ok(api.get("/api/work-orders", headers=field))["orders"] == []
                offline.clear()
                (state / "planned").touch()
                wait_for(lambda: (state / "deferred").exists(), "temporary refusal persisted", process)
                assert (endpoint, 409) in attempts
                if scenario == "started":
                    order_started.set()
                else:
                    ok(api.put(itinerary + "/cancel", headers=admin, json={"revision": order["revision"]}))
                if scenario == "stale":
                    other = report(target)
                    other["payload"]["answers"]["FIN.03"] = {"value": "", "notes": "Outros dados / other device."}
                    ok(api.put(endpoint, headers=field, json=other))
                (state / "unblocked").touch()
                assert process.wait(timeout=65) == 0
                remote = ok(api.get(endpoint, headers=field))
                if scenario == "stale":
                    assert remote["payload"]["answers"]["FIN.03"]["notes"] == "Outros dados / other device."
                    assert remote["revision"] == 1 and remote["photos"] == []
                else:
                    assert remote["id"] == rid and remote["payload"]["answers"]["FIN.03"]["notes"] == OBSERVATION
                    photo, = remote["photos"]
                    original = (state / "original.png").read_bytes()
                    assert photo["digest"] == sha256(original).hexdigest()
                    assert api.get(endpoint + "/photos/" + photo["id"], headers=field).content == original
                print(f"PASS: {scenario} deferral after restart and correct report/photo handling.", flush=True)
            finally:
                if process is not None and process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
                runner.should_exit = True
                worker.join(timeout=12)
                assert not worker.is_alive()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("port", type=int)
    args = parser.parse_args()
    for scenario in ("legacy", "started", "stale"):
        exercise(args.executable.resolve(), args.port, scenario)
