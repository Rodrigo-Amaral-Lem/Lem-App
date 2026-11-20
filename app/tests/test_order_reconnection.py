# SPDX-License-Identifier: GPL-3.0-only
"""Run the native controller through a shift handoff against a disposable API."""

from contextlib import ExitStack
from hashlib import sha256
import argparse
import os
from pathlib import Path
import secrets
import socket
import subprocess
import tempfile
from threading import Thread
from time import monotonic, sleep

from fastapi.testclient import TestClient
import uvicorn

import sys
server_dir = Path(os.environ.get("LEM_SERVER_REPO", Path(__file__).resolve().parents[3] / "Server")) / "server"
sys.path[:0] = [str(server_dir), str(server_dir / "tests")]
from app.compatibility import VERSION
from app.main import create_app
from uuid import uuid4
from test_visit_sync_integrity import plan, report
from test_work_orders import account
from test_workflow import upload


def ok(response):
    response.raise_for_status()
    return response.json()


def wait_for(predicate, label, process=None):
    deadline = monotonic() + 25
    while not predicate():
        if process is not None and process.poll() is not None:
            raise RuntimeError(f"Native controller exited {process.returncode} before {label}")
        if monotonic() >= deadline:
            raise TimeoutError(label)
        sleep(0.05)


def exercise(executable, port):
    password = "Lm7!" + secrets.token_hex(16)
    os.environ.update(ADMIN_EMAIL="admin@lem.pt", ADMIN_PASSWORD=password)
    with tempfile.TemporaryDirectory(prefix="work-order-native-") as directory, socket.socket() as listener:
        state = Path(directory)
        listener.bind(("127.0.0.1", port))
        app = create_app("sqlite:///" + str(state / "fixture.db"))
        with ExitStack() as resources:
            resources.callback(app.state.factory.kw["bind"].dispose)
            api = resources.enter_context(TestClient(app, headers={"X-Lem-Client-Version": VERSION}))
            login = ok(api.post("/api/login", json={"email": "admin@lem.pt", "password": password}))
            admin = {"Authorization": "Bearer " + login["token"]}
            field_password = "Shift8!" + secrets.token_hex(12)
            staff = ok(api.post("/api/users", headers=admin, json={
                "name": "Old shift", "email": "old-shift@example.invalid", "role": "tech", "password": field_password,
            }))
            token = ok(api.post("/api/login", json={"email": "old-shift@example.invalid", "password": field_password}))
            old_shift = {"Authorization": "Bearer " + token["token"], "X-Lem-Work-Orders": "1"}
            next_shift, _ = account(api, admin, "new-shift")
            contract = ok(api.post("/api/contracts", headers=admin, json={
                "company": "Âncora / Field", "year": 2026, "semester": 2, "kind": "MP",
            }))
            ok(upload(api, admin, contract["id"], True))
            target = ok(api.get("/api/work-order-candidates", headers=admin))[0]["target"]
            order, item, itinerary = plan(api, admin, [staff["id"]], target)
            rid = str(uuid4())
            path = "/api/reports/" + rid
            draft = ok(api.put(path, headers=old_shift, json={**report(target), "work_item_id": item}))
            runner = uvicorn.Server(uvicorn.Config(app, log_level="info"))
            worker = Thread(target=runner.run, kwargs={"sockets": [listener]}, daemon=True)
            worker.start()
            process = None
            try:
                wait_for(lambda: runner.started, "private API startup")
                process = subprocess.Popen([
                    str(executable), "--restored-visit", f"http://127.0.0.1:{port}", str(state), itinerary["id"], item, target, draft["id"],
                ], env={**os.environ, "QT_QPA_PLATFORM": "offscreen", "QT_FORCE_STDERR_LOGGING": "1",
                        "LEM_ORDER_TEST_PASSWORD": field_password, "LEM_TEST_CLIENT_VERSION": VERSION})
                print(f"Owned native controller PID {process.pid}, parent PID {os.getpid()}, loopback port {port}.",
                      flush=True)
                wait_for(lambda: (state / "opened").exists(), "native report opened", process)
                removed = ok(api.put(order, headers=admin, json={**itinerary, "revision": 1,
                                                     "technicians": [next_shift]}))
                (state / "scope-removed").touch()
                wait_for(lambda: (state / "upload-rejected").exists(), "native sync rejection", process)
                assert api.get(path, headers=old_shift).status_code == 403
                ok(api.put(order, headers=admin, json={**itinerary, "revision": removed["revision"]}))
                restored = ok(api.get(path, headers=old_shift))
                assert restored["revision"] == draft["revision"] and restored["work_item_id"] == item
                (state / "scope-restored").touch()
                assert process.wait(timeout=40) == 0
                remote = ok(api.get(path, headers=old_shift))
                assert remote["id"] == rid and remote["work_item_id"] == item and remote["revision"] > draft["revision"]
                assert remote["payload"]["answers"]["FIN.03"]["notes"] == "Ângulo revisto, ligação confirmada / field follow-up."
                photo, = remote["photos"]
                original = (state / "original.png").read_bytes()
                assert photo["digest"] == sha256(original).hexdigest()
                assert api.get(path + "/photos/" + photo["id"], headers=old_shift).content == original
                print("PASS: restored visit after restart, same UUID, mixed-language note and matching original bytes.")
            finally:
                if process is not None and process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
                runner.should_exit = True
                worker.join(12)
                assert not worker.is_alive()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("port", type=int)
    arguments = parser.parse_args()
    exercise(arguments.executable.resolve(), arguments.port)
