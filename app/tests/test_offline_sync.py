"""Disposable local HTTP integration runner for the C++ offline_sync executable.
Usage: python app/tests/test_offline_sync.py /path/to/Lem-offline-test
Requires the server's dependencies plus httpx and openpyxl. No real server access.
"""

import os, sys, time, tempfile, subprocess, socket, json
from pathlib import Path
import openpyxl, httpx


def main(binary):
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="lem-offline-") as td:
        folder = Path(td)
        xlsx = folder / "chargers.xlsx"
        w = openpyxl.Workbook()
        s = w.active
        s.title = "Parque"
        s.append(["Identificador", "Modelo", "Série", "Tomadas"])
        s.append(["PERF-001", "DC60", "0001", "CCS2, CCS2"])
        w.save(xlsx)
        runner = folder / "runner.py"
        runner.write_text("""import asyncio,json,sys
from fastapi.responses import JSONResponse
from app.main import create_app
app=create_app();counts={};lost=False;lost_delete=False;arm_delete=False;photo_active=0;photo_max=0;photo_gets=0
@app.middleware('http')
async def track(request,call_next):
 global lost,lost_delete,arm_delete,photo_active,photo_max,photo_gets
 key=request.method+' '+request.url.path;counts[key]=counts.get(key,0)+1
 payload={}
 if request.method=='PUT' and '/api/reports/' in request.url.path:
  if '/photos/' in request.url.path:await asyncio.sleep(.3)
  else:
   payload=await request.json();await asyncio.sleep(.3)
   marker=payload.get('payload',{}).get('answers',{}).get('FIN.03',{}).get('value')
   if marker=='LOSE_DELETE_ACK':arm_delete=True
   if marker=='REJECT_413':return JSONResponse({'detail':'simulated report rejection'},status_code=413)
 if request.method=='GET' and '/photos/' in request.url.path:
  photo_gets+=1;request_photo=photo_gets;photo_active+=1;photo_max=max(photo_max,photo_active)
  try:
   await asyncio.sleep(.2)
   if request_photo==9:return JSONResponse({'detail':'simulated interruption'},status_code=503)
   response=await call_next(request)
  finally:photo_active-=1
 else:response=await call_next(request)
 if arm_delete and not lost_delete and request.method=='DELETE' and '/photos/' in request.url.path and response.status_code==200:
  lost_delete=True;return JSONResponse({})
 if not lost and response.status_code==200 and payload.get('payload',{}).get('answers',{}).get('FIN.03',{}).get('value')=='LOSE_ACK':
  lost=True;return JSONResponse({})
 return response
@app.get('/__test/stats')
def stats():return {'requests':counts,'photo_max':photo_max,'lost_delete':lost_delete}
""")
        env = {
            **os.environ,
            "PYTHONPATH": os.pathsep.join(filter(None, [str(Path(os.environ.get("LEM_SERVER_REPO", str(root.parent / "Server"))) / "server"), os.environ.get("PYTHONPATH", "") ])),
            "ADMIN_EMAIL": "admin@lem.pt",
            "ADMIN_PASSWORD": "IntegrationPassword123!",
            "DATABASE_URL": "sqlite:///" + str(folder / "test.db"),
            "NO_PROXY": "localhost,127.0.0.1",
            "QT_QPA_PLATFORM": "offscreen",
        }
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 18765))
        with (folder / "server.log").open("w+") as log:
            server = subprocess.Popen(
                [
                    sys.executable,
                    "-m",
                    "uvicorn",
                    "runner:app",
                    "--host",
                    "127.0.0.1",
                    "--port",
                    "18765",
                ],
                cwd=folder,
                env=env,
                stdout=log,
                stderr=log,
            )
            try:
                with httpx.Client(trust_env=False) as client:
                    for _ in range(100):
                        try:
                            if client.get("http://127.0.0.1:18765/api/health").status_code == 200:
                                break
                        except httpx.HTTPError:
                            pass
                        if server.poll() is not None:
                            raise RuntimeError("Local test API failed to start")
                        time.sleep(0.1)
                    result = subprocess.run(
                        [str(Path(binary).resolve()), "--network", "--xlsx", str(xlsx)],
                        env=env,
                        capture_output=True,
                        text=True,
                        timeout=90,
                    )
                    print(result.stdout, result.stderr)
                    assert result.returncode == 0, result.returncode
                    stats = client.get("http://127.0.0.1:18765/__test/stats").json()
                    counts = stats["requests"]
                    assert stats["photo_max"] <= 2, stats
                    assert stats["lost_delete"], stats
                    downloads = sum(
                        v for k, v in counts.items() if k.startswith("GET ") and "/photos/" in k
                    )
                    assert downloads == 14, counts
                    # Two contract selections + import + explicit deletion refresh; sync batches do not fetch catalogues.
                    catalogues = sum(
                        v
                        for k, v in counts.items()
                        if k.startswith("GET ") and k.endswith("/chargers")
                    )
                    assert catalogues == 4, counts
                    print(
                        "PASS: bounded bulk concurrency, cached-photo reuse, cancel/resume and partial retry; no catalogue reload after sync; no live service accessed"
                    )
            finally:
                server.terminate()
                server.wait(timeout=10)


if __name__ == "__main__":
    main(sys.argv[1])
