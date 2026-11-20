# SPDX-License-Identifier: GPL-3.0-only
"""Run with the Lem-offline-test executable and an individually reserved loopback port."""
from hashlib import sha256
import os
from pathlib import Path
import secrets
import socket
import subprocess
import sys
import tempfile
from threading import Thread
import time
from uuid import uuid4

server_directory = Path(os.environ.get("LEM_SERVER_REPO", Path(__file__).resolve().parents[3] / "Server")) / "server"
sys.path[:0] = [str(server_directory), str(server_directory / "tests")]
from fastapi.testclient import TestClient
import uvicorn
from app.compatibility import VERSION
from app.main import create_app, TEMPLATE
from app.work_orders import lisbon_now
from test_workflow import add_signature, answers, upload


def verify(binary, port):
    password = "Ab7!" + secrets.token_hex(16)
    os.environ.update(ADMIN_EMAIL="admin@lem.pt", ADMIN_PASSWORD=password)
    with tempfile.TemporaryDirectory() as scratch, socket.socket() as listener:
        listener.bind(("127.0.0.1", port))
        app = create_app("sqlite:///" + str(Path(scratch) / "orders.db"))
        with TestClient(app, headers={"X-Lem-Client-Version": VERSION}) as api:
            def login(email):
                response = api.post("/api/login", json={"email":email, "password":password})
                response.raise_for_status()
                return {"Authorization":"Bearer " + response.json()["token"]}
            admin = login("admin@lem.pt")
            def new_account(name, role):
                email = name + "@example.invalid"
                response = api.post("/api/users", headers=admin, json={
                    "name":name, "email":email, "role":role, "password":password})
                response.raise_for_status()
                return response.json()["id"], login(email)
            uid, tech = new_account("field", "tech")
            _, other_admin = new_account("observer", "admin")
            response = api.post("/api/contracts", headers=admin, json={
                "company":"Oficina", "year":2026, "semester":1, "kind":"MP"})
            response.raise_for_status()
            upload(api, admin, response.json()["id"], True).raise_for_status()
            candidates = api.get("/api/work-order-candidates", headers=admin).json()
            reports = []
            for candidate, creator in zip(candidates, (admin, other_admin), strict=True):
                oid, rid = str(uuid4()), str(uuid4())
                day = lisbon_now().date().isoformat()
                api.put("/api/work-orders/" + oid, headers=creator, json=dict(id=oid, revision=0,
                    start=day, due=day, technicians=[uid], targets=[candidate["target"]])).raise_for_status()
                url = "/api/reports/" + rid
                order = next(row for row in api.get("/api/work-orders", headers=tech).json()["orders"] if row["id"] == oid)
                body = dict(charger_id=candidate["target"], work_item_id=order["items"][0]["id"], checklist_version=TEMPLATE["version"],
                            base_revision=0, status="draft", payload={"answers":{}})
                api.put(url, headers=tech, json=body).raise_for_status()
                payload = {"answers": answers()}
                pid = add_signature(api, tech, rid, payload)
                api.put(url, headers=tech, json={**body,"status":"closed","base_revision":1,"payload":payload}).raise_for_status()
                original = api.get(url + "/photos/" + pid, headers=admin)
                original.raise_for_status()
                reports.append((rid, sha256(original.content).hexdigest()))
        runner = uvicorn.Server(uvicorn.Config(app, log_level="info"))
        thread = Thread(target=runner.run, kwargs={"sockets":[listener]}, daemon=True)
        thread.start()
        try:
            for _ in range(100):
                if runner.started: break
                time.sleep(.05)
            assert runner.started
            subprocess.run([str(binary), "--work-order-download", f"http://127.0.0.1:{port}",
                            reports[0][0], reports[1][0], reports[0][1]], check=True, timeout=75,
                           env={**os.environ, "LEM_ORDER_TEST_PASSWORD":password,"LEM_TEST_CLIENT_VERSION":VERSION,
                                "QT_QPA_PLATFORM":"offscreen", "QT_FORCE_STDERR_LOGGING":"1"})
        finally:
            runner.should_exit = True
            thread.join(15)
            app.state.factory.kw["bind"].dispose()


if __name__ == "__main__":
    verify(Path(sys.argv[1]).resolve(), int(sys.argv[2]))
