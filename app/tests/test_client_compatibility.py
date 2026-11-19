# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Run the native client against disposable server policies; no live server access.
Usage: python app/tests/test_client_compatibility.py /path/to/Lem-compatibility-test
"""

import os
import re
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import httpx

RUNNER = """from fastapi.responses import JSONResponse
from app.main import create_app
import os
app=create_app();mode=os.environ['TEST_MODE'];calls=[];unavailable=False
@app.middleware('http')
async def fixture(request,call_next):
 if request.url.path.startswith('/api/'):
  calls.append([request.method,request.url.path,request.headers.get('X-Lem-Client-Version')])
 if request.url.path=='/api/client-compatibility':
  if mode=='missing':return JSONResponse({'detail':'not found'},status_code=404)
  if mode=='malformed':return JSONResponse({'compatible':True})
  if mode=='wrong-echo':
   p=app.state.client_policy.evaluate(os.environ['LEM_TEST_CLIENT_VERSION']);p['current_version']='0.0.0';return JSONResponse(p)
 if mode=='race' and request.url.path=='/api/login':app.state.client_policy.minimum=os.environ['LEM_TEST_NEWER_VERSION']
 if unavailable and request.url.path=='/api/contracts':
  return JSONResponse({'detail':'temporarily unavailable'},status_code=503)
 return await call_next(request)
@app.get('/__test/stats')
def stats():return {'calls':calls}
@app.post('/__test/availability/{offline}')
def availability(offline: bool):
 global unavailable
 unavailable=offline
@app.post('/__test/revoke')
def revoke():
 from app import admin_cli
 return admin_cli.execute(app.state.factory, admin_cli.parser().parse_args(
  ['users','revoke-sessions','--all']))
"""


def check_sessions(binary, http, url, profile, env):
    profile.mkdir()

    def run(action):
        result = subprocess.run(
            [str(binary), url, action, str(profile)], env=env, capture_output=True,
            text=True, errors="replace", timeout=25,
        )
        assert result.returncode == 0, (action, result.stdout, result.stderr)

    run("login")
    run("resume")
    http.post(url + "/__test/availability/true").raise_for_status()
    run("offline")
    http.post(url + "/__test/availability/false").raise_for_status()
    run("resume")
    assert http.post(url + "/__test/revoke").json()["sessions_revoked"] == 1
    run("rejected")
    run("signed-out")
    run("login")
    run("switch")
    run("reset-server")
    run("signed-out")
    run("login")
    run("logout")
    run("signed-out")
    calls = http.get(url + "/__test/stats").json()["calls"]
    assert sum(c[:2] == ["POST", "/api/login"] for c in calls) == 3
    assert sum(c[:2] == ["POST", "/api/logout"] for c in calls) == 1
    print("PASS: cold startup, transient network failure, revocation, server switch and logout")


def main(binary, sessions=False):
    root = Path(__file__).resolve().parents[2]
    binary = Path(binary).resolve()
    current = re.search(r'project\(Lem VERSION ([0-9.]+)', (root / 'app/CMakeLists.txt').read_text()).group(1)
    major, minor, patch = map(int, current.split('.'))
    newer = f'{major}.{minor}.{patch + 1}'
    modes = ["latest", "update", "incompatible", "missing", "malformed", "wrong-echo", "race"]
    if sessions:
        modes.append("session")
    for mode in modes:
        with tempfile.TemporaryDirectory(prefix="lem-version-") as name:
            folder = Path(name)
            (folder / "runner.py").write_text(RUNNER)
            with socket.socket() as probe:
                probe.bind(("127.0.0.1", 0))
                port = probe.getsockname()[1]
            minimum = newer if mode == "incompatible" else current
            latest = newer if mode in ["incompatible", "update", "race", "session"] else current
            env = {
                **os.environ,
                "PYTHONPATH": os.pathsep.join(filter(None, [str(Path(os.environ.get("LEM_SERVER_REPO", str(root.parent / "Server"))) / "server"), os.environ.get("PYTHONPATH", "")])),
                "TEST_MODE": mode,
                "LEM_TEST_CLIENT_VERSION": current,
                "LEM_TEST_NEWER_VERSION": newer,
                "ADMIN_EMAIL": "admin@lem.pt",
                "ADMIN_PASSWORD": "CompatibilityPassword123!",
                "DATABASE_URL": "sqlite:///" + str(folder / "test.db"),
                "LEM_MIN_CLIENT_VERSION": minimum,
                "LEM_LATEST_CLIENT_VERSION": latest,
                "LEM_UPDATE_URL": "",
                "QT_QPA_PLATFORM": "offscreen",
                "NO_PROXY": "localhost,127.0.0.1",
            }
            with (folder / "server.log").open("w") as log:
                server = subprocess.Popen(
                    [
                        sys.executable,
                        "-m",
                        "uvicorn",
                        "runner:app",
                        "--host",
                        "127.0.0.1",
                        "--port",
                        str(port),
                    ],
                    cwd=folder,
                    env=env,
                    stdout=log,
                    stderr=log,
                )
                try:
                    url = f"http://127.0.0.1:{port}"
                    with httpx.Client(trust_env=False) as http:
                        for _ in range(100):
                            try:
                                if http.get(url + "/api/health").status_code == 200:
                                    break
                            except httpx.HTTPError:
                                pass
                            if server.poll() is not None:
                                raise RuntimeError("Fixture API did not start")
                            time.sleep(0.05)
                        if mode == "session":
                            check_sessions(binary, http, url, folder / "client-profile", env)
                            continue
                        expected = mode if mode in ["latest", "update"] else "blocked"
                        result = subprocess.run(
                            [str(binary), url, expected],
                            env=env,
                            capture_output=True,
                            text=True,
                            errors="replace",  # Native Qt diagnostics may use the Windows code page.
                            timeout=20,
                        )
                        assert result.returncode == 0, (mode, result.stdout, result.stderr)
                        calls = http.get(url + "/__test/stats").json()["calls"]
                        logins = [c for c in calls if c[:2] == ["POST", "/api/login"]]
                        assert len(logins) == (1 if mode in ["latest", "update", "race"] else 0), (
                            mode,
                            calls,
                        )
                        assert all(c[2] == current for c in calls if c[1] != "/api/health"), calls
                        print("PASS:", mode, "login requests:", len(logins))
                finally:
                    # Windows venv launchers can own a separate Python child;
                    # stop this disposable process tree before deleting its log/DB.
                    if os.name == "nt" and server.poll() is None:
                        subprocess.run(["taskkill", "/PID", str(server.pid), "/T", "/F"],
                                       capture_output=True, timeout=10)
                    else:
                        server.terminate()
                    server.wait(timeout=10)


if __name__ == "__main__":
    main(sys.argv[1], "--sessions" in sys.argv[2:])
