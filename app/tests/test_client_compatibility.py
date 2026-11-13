"""Run the native client against disposable server policies; no live server access.
Usage: python app/tests/test_client_compatibility.py /path/to/Lem-compatibility-test
"""

import os
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
app=create_app();mode=os.environ['TEST_MODE'];calls=[]
@app.middleware('http')
async def fixture(request,call_next):
 if request.url.path.startswith('/api/'):
  calls.append([request.method,request.url.path,request.headers.get('X-Lem-Client-Version')])
 if request.url.path=='/api/client-compatibility':
  if mode=='missing':return JSONResponse({'detail':'not found'},status_code=404)
  if mode=='malformed':return JSONResponse({'compatible':True})
  if mode=='wrong-echo':
   p=app.state.client_policy.evaluate('0.1.32');p['current_version']='0.1.31';return JSONResponse(p)
 if mode=='race' and request.url.path=='/api/login':app.state.client_policy.minimum='0.1.33'
 return await call_next(request)
@app.get('/__test/stats')
def stats():return {'calls':calls}
"""


def main(binary):
    root = Path(__file__).resolve().parents[2]
    for mode in ["latest", "update", "incompatible", "missing", "malformed", "wrong-echo", "race"]:
        with tempfile.TemporaryDirectory(prefix="lem-version-") as name:
            folder = Path(name)
            (folder / "runner.py").write_text(RUNNER)
            with socket.socket() as probe:
                probe.bind(("127.0.0.1", 0))
                port = probe.getsockname()[1]
            minimum = "0.1.33" if mode == "incompatible" else "0.1.28"
            latest = (
                "0.1.34"
                if mode == "incompatible"
                else "0.1.33" if mode in ["update", "race"] else "0.1.32"
            )
            env = {
                **os.environ,
                "PYTHONPATH": str(Path(os.environ.get("LEM_SERVER_REPO", str(root.parent / "Server"))) / "server"),
                "TEST_MODE": mode,
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
                        expected = mode if mode in ["latest", "update"] else "blocked"
                        result = subprocess.run(
                            [str(Path(binary).resolve()), url, expected],
                            env=env,
                            capture_output=True,
                            text=True,
                            timeout=20,
                        )
                        assert result.returncode == 0, (mode, result.stdout, result.stderr)
                        calls = http.get(url + "/__test/stats").json()["calls"]
                        logins = [c for c in calls if c[:2] == ["POST", "/api/login"]]
                        assert len(logins) == (1 if mode in ["latest", "update", "race"] else 0), (
                            mode,
                            calls,
                        )
                        assert all(c[2] == "0.1.32" for c in calls if c[1] != "/api/health"), calls
                        print("PASS:", mode, "login requests:", len(logins))
                finally:
                    server.terminate()
                    server.wait(timeout=10)


if __name__ == "__main__":
    main(sys.argv[1])
