# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Disposable local HTTP integration runner for the C++ offline_sync executable.
Usage: python app/tests/test_offline_sync.py /path/to/Lem-offline-test
Use --delete-ack for the focused two-process test, or --legacy-delete-ack to replay
the old server response in the race.
Requires the server's dependencies plus httpx and openpyxl. No real server access.
"""

import argparse
import os, sys, time, tempfile, subprocess, socket, json
from contextlib import contextmanager
from io import BytesIO
from zipfile import ZipFile
from pypdf import PdfReader
from pathlib import Path
import openpyxl, httpx


@contextmanager
def temporary_workspace():
    folder = tempfile.TemporaryDirectory(prefix="lem-offline-")
    try:
        yield folder.name
    finally:
        # Windows can briefly retain inherited log handles after the server exits.
        # Retry only this owned temporary directory; never hide a persistent lock
        # or let a transient cleanup error obscure a successful functional run.
        for attempt in range(20):
            try:
                folder.cleanup()
                break
            except PermissionError:
                if attempt == 19:
                    raise
                time.sleep(0.1)


def main(binary, delete_ack=False, legacy_delete_ack=False):
    root = Path(__file__).resolve().parents[2]
    with temporary_workspace() as td:
        folder = Path(td)
        xlsx = folder / "chargers.xlsx"
        w = openpyxl.Workbook()
        s = w.active
        s.title = "Parque"
        s.append(["Identificador", "Modelo", "Série", "Tomadas"])
        s.append(["PERF-001", "DC60", "0001", "CCS2, CCS2"])
        w.save(xlsx)
        runner = folder / "runner.py"
        branding_output = Path(os.environ.get("LEM_BRANDING_TEST_OUTPUT", str(folder / "branding")))
        branding_output.mkdir(parents=True, exist_ok=True)
        runner.write_text("""import asyncio,json,os,sys
from fastapi.responses import JSONResponse
from app.main import create_app
from app.models import ImportBatch
from sqlalchemy import func, select
app=create_app();counts={};lost=False;lost_delete=False;arm_delete=False;photo_active=0;photo_max=0;photo_gets=0;lost_correction=False;fail_contract_refresh=False
legacy_identity_conflict = False
lost_import_path = None
import_replayed_once = False
@app.middleware('http')
async def track(request,call_next):
 global lost,lost_delete,arm_delete,photo_active,photo_max,photo_gets,lost_correction,fail_contract_refresh
 global legacy_identity_conflict, lost_import_path, import_replayed_once
 key=request.method+' '+request.url.path;counts[key]=counts.get(key,0)+1
 if request.method == 'POST' and request.url.path.endswith('/correction/commit') and not legacy_identity_conflict:
  legacy_identity_conflict = True
  return JSONResponse({'detail': 'Os dados do carregador foram corrigidos noutro report; atualize antes de concluir. Campo: model'}, status_code=409)
 if fail_contract_refresh and key=='GET /api/contracts':
  fail_contract_refresh=False;return JSONResponse({'detail':'simulated failed refresh after settings save'},status_code=503)
 payload={}
 if request.method=='PUT' and '/api/reports/' in request.url.path:
  if '/photos/' in request.url.path:await asyncio.sleep(.3)
  else:
   payload=await request.json();await asyncio.sleep(.3)
   marker=payload.get('payload',{}).get('answers',{}).get('FIN.03',{}).get('notes')
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
 if os.environ.get("LEM_TEST_IMPORT_REPLAY") == "1" and request.method == "POST" and request.url.path.endswith("/import") and response.status_code == 200:
  if lost_import_path is None and counts[key] == 2:  # preview, then accepted commit
   lost_import_path = request.url.path
   return JSONResponse({})  # committed; acknowledgement unreadable to the client
  if request.url.path == lost_import_path and counts[key] == 3:
   with app.state.factory() as db:
    import_replayed_once = db.scalar(select(func.count(ImportBatch.id))) == 1
 if request.method=='PUT' and request.url.path.endswith('/presentation') and response.status_code==200:fail_contract_refresh=True
 if arm_delete and not lost_delete and request.method=='DELETE' and '/photos/' in request.url.path and response.status_code==200:
  lost_delete=True;return JSONResponse({})
 if not lost and response.status_code==200 and payload.get('payload',{}).get('answers',{}).get('FIN.03',{}).get('notes')=='LOSE_ACK':
  lost=True;return JSONResponse({})
 if not lost_correction and request.url.path.endswith('/correction/commit') and response.status_code==200:
  lost_correction=True;return JSONResponse({})
 return response
@app.get('/__test/stats')
def stats():return {'requests':counts,'import_replayed_once':import_replayed_once,'photo_max':photo_max,'lost_delete':lost_delete,'lost_correction':lost_correction,'legacy_identity_conflict':legacy_identity_conflict}
""" + ("""
import os
import httpx

delete_cases = {}
delete_faults = {}
legacy_delete_ack = os.environ.get('LEM_TEST_LEGACY_DELETE_ACK') == '1'

@app.middleware('http')
async def delete_ack_faults(request, call_next):
    parts = request.url.path.strip('/').split('/')
    if parts[:2] != ['api', 'reports'] or len(parts) not in (3, 5):
        return await call_next(request)
    report_id = parts[2]
    if len(parts) == 3 and request.method == 'PUT':
        body = await request.json()
        marker = body['payload']['answers'].get('FIN.03', {}).get('notes')
        if marker in ('RACE_DELETE_ACK', 'DROP_UPLOAD_ACK', 'DROP_DELETE_ACK'):
            delete_cases.setdefault(report_id, marker)
        return await call_next(request)
    case = delete_cases.get(report_id)
    if len(parts) != 5 or case in delete_faults:
        return await call_next(request)
    path = '/api/reports/' + report_id
    if case == 'RACE_DELETE_ACK' and request.method == 'DELETE':
        # A has finished its GET. Give B a distinct session and write through
        # the real API immediately before dispatching A's DELETE.
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app),
                                     base_url='http://fixture') as writer:
            version = {'X-Lem-Client-Version': request.headers['X-Lem-Client-Version']}
            login = await writer.post('/api/login', headers=version, json={
                'email': 'admin@lem.pt', 'password': 'IntegrationPassword123!',
            })
            login.raise_for_status()
            headers = {**version, 'Authorization': 'Bearer ' + login.json()['token']}
            current = (await writer.get(path, headers=headers)).raise_for_status().json()
            assert current['revision'] == int(request.query_params['base_revision'])
            assert all(photo['id'] != parts[4] for photo in current['photos'])
            payload = current['payload']
            payload['answers']['FIN.03']['notes'] = 'Another device saved this answer'
            saved = await writer.put(path, headers=headers, json={
                'charger_id': current['charger_id'], 'base_revision': current['revision'],
                'checklist_version': current['template']['version'], 'status': 'draft', 'payload': payload,
            })
            revision = saved.raise_for_status().json()['revision']
        response = await call_next(request)
        delete_faults[case] = {'report_id': report_id, 'revision': revision, 'status': response.status_code}
        if legacy_delete_ack:
            return JSONResponse({'id': parts[4], 'deleted': True, 'revision': revision})
        return response
    response = await call_next(request)
    if response.status_code == 200 and (
        (case == 'DROP_UPLOAD_ACK' and request.method == 'PUT') or
        (case == 'DROP_DELETE_ACK' and request.method == 'DELETE')
    ):
        delete_faults[case] = {'report_id': report_id, 'photo_id': parts[4]}
        # The write committed. Suppress its application acknowledgement.
        return JSONResponse({'detail': 'test fixture discarded committed response'}, status_code=503)
    return response

@app.get('/__test/delete-ack')
def delete_ack_stats():
    return delete_faults
""" if delete_ack else ""))
        env = {
            **os.environ,
            "PYTHONPATH": os.pathsep.join(filter(None, [str(Path(os.environ.get("LEM_SERVER_REPO", str(root.parent / "Server"))) / "server"), os.environ.get("PYTHONPATH", "") ])),
            "ADMIN_EMAIL": "admin@lem.pt",
            "ADMIN_PASSWORD": "IntegrationPassword123!",
            "DATABASE_URL": "sqlite:///" + str(folder / "test.db"),
            "NO_PROXY": "localhost,127.0.0.1",
            "QT_QPA_PLATFORM": "offscreen",
            "LEM_TEST_IMPORT_REPLAY": "0" if delete_ack else "1",
            "LEM_TEST_LEGACY_DELETE_ACK": "1" if legacy_delete_ack else "0",
            "LEM_BRANDING_TEST_OUTPUT": str(branding_output.resolve()),
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
            print(f"Owned test API process {server.pid} on 127.0.0.1:18765", flush=True)
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
                    command = [str(Path(binary).resolve())]
                    if os.environ.get("LEM_TEST_GDB"):
                        command = [os.environ["LEM_TEST_GDB"], "-batch", "-ex", "run", "-ex", "bt", "--args", *command]

                    def run_native(*args):
                        result = subprocess.run(
                            [*command, *args], env=env, capture_output=True, text=True,
                            encoding="mbcs" if os.name == "nt" else "utf-8", timeout=180,
                        )
                        print(result.stdout, result.stderr)
                        return result.returncode

                    args = ("--network", "--xlsx", str(xlsx))
                    if delete_ack:
                        check_delete_ack(client, run_native, args, folder)
                        return
                    assert run_native(*args, "--retry-import") == 0
                    for name in ('legacy', 'legacy-admin'):
                        direct = PdfReader(branding_output / (name + '.pdf'))
                        assert 'CSMTEC' in direct.pages[0].extract_text()
                        with ZipFile(branding_output / (name + '.zip')) as archive:
                            report = json.loads(archive.read(next(name for name in archive.namelist() if name.endswith(".json"))))
                            assert report['presentation']['issuer'] == 'CSMTEC'
                            bundled = PdfReader(BytesIO(archive.read(report['identifier'] + '.pdf')))
                            assert 'CSMTEC' in bundled.pages[0].extract_text()
                            assert direct.pages[0].extract_text() == bundled.pages[0].extract_text()
                    stats = client.get("http://127.0.0.1:18765/__test/stats").json()
                    counts = stats["requests"]
                    assert stats["import_replayed_once"], stats
                    assert stats["photo_max"] <= 4, stats
                    assert stats["lost_delete"], stats
                    assert stats["legacy_identity_conflict"], stats
                    assert stats["lost_correction"], stats
                    # The technician uploaded the original fixtures. Only the new attachment
                    # should cross the wire during the administrator's correction.
                    posts = [key.removeprefix("POST ") for key in counts if key.startswith("POST ")]
                    sources = {path.removesuffix("/corrections") for path in posts if path.endswith("/corrections")}
                    copies = {path.removesuffix("/correction/commit") for path in posts if path.endswith("/correction/commit")}
                    assert sources and len(copies) == 1
                    media_writes = {}
                    for key, count in counts.items():
                        method, path = key.split(" ", 1)
                        report, separator, _ = path.partition("/photos/")
                        if separator and report in sources | copies:
                            assert method != "GET", key
                            if report in copies:
                                media_writes[method] = media_writes.get(method, 0) + count
                    assert media_writes == {"PUT": 1, "DELETE": 1}, media_writes
                    print("PASS: correction used zero evidence GETs, one new upload and one removal")
                    downloads = sum(
                        v for k, v in counts.items() if k.startswith("GET ") and "/photos/" in k
                    )
                    assert 13 <= downloads <= 14, counts
                    # Manual sync receives catalogue state; automatic uploads remain deferred/batched.
                    catalogues = sum(
                        v
                        for k, v in counts.items()
                        if k.startswith("GET ") and k.endswith("/chargers")
                    )
                    assert 6 <= catalogues < 80, counts
                    print(
                        "PASS: bounded bulk concurrency, cached-photo reuse, cancel/resume and partial retry; manual sync receives remote state; no live service accessed"
                    )
            finally:
                server.terminate()
                server.wait(timeout=10)


def check_delete_ack(client, run_native, args, folder):
    state = ("--state", str(folder))
    first = run_native(*args, *state, "--delete-ack")
    url = "http://127.0.0.1:18765"
    auth = {"X-Lem-Client-Version": os.environ.get("LEM_TEST_CLIENT_VERSION", "0.5.4")}
    login = client.post(url + "/api/login", headers=auth,
                        json={"email": "admin@lem.pt", "password": "IntegrationPassword123!"})
    auth["Authorization"] = "Bearer " + login.raise_for_status().json()["token"]
    faults = client.get(url + "/__test/delete-ack").raise_for_status().json()

    def report(case):
        return client.get(f"{url}/api/reports/{faults[case]['report_id']}",
                          headers=auth).raise_for_status().json()

    raced = report("RACE_DELETE_ACK")
    race_answer = raced["payload"]["answers"]["FIN.03"]["notes"]
    print(f"Stale DELETE HTTP {faults['RACE_DELETE_ACK']['status']}; "
          f"remote revision {raced['revision']}, status {raced['status']}, answer {race_answer!r}")
    assert first == 0, first
    assert faults["RACE_DELETE_ACK"]["status"] == 409
    assert race_answer == "Another device saved this answer"
    assert raced["revision"] == faults["RACE_DELETE_ACK"]["revision"] and raced["status"] == "draft"
    assert report("DROP_UPLOAD_ACK")["photos"] == []

    dropped = faults["DROP_DELETE_ACK"]
    deleted = report("DROP_DELETE_ACK")
    base = json.loads((folder / "resume.json").read_text())["base"]
    assert deleted["revision"] == base + 1
    assert all(photo["id"] != dropped["photo_id"] for photo in deleted["photos"])
    assert run_native(*state, "--resume-delete-ack") == 0
    closed = report("DROP_DELETE_ACK")
    # Closing adds the finish timestamp in a draft PUT before the final closure PUT.
    assert (closed["revision"], closed["status"]) == (base + 4, "closed")
    assert closed["payload"]["answers"]["FIN.03"]["notes"] == "Newer local answer after lost response"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--delete-ack", action="store_true", help="run the focused DELETE ACK integration")
    modes.add_argument("--legacy-delete-ack", action="store_true", help="also replay the old server's unsafe ACK")
    options = parser.parse_args()
    main(options.binary, options.delete_ack or options.legacy_delete_ack, options.legacy_delete_ack)
