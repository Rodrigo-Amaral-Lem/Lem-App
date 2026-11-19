# SPDX-License-Identifier: GPL-3.0-only
"""Validate native ZIPs with Python's independent reader and original evidence hashes."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
from zipfile import ZipFile, ZIP_STORED
from PIL import Image
from io import BytesIO
from pypdf import PdfReader

binary = Path(sys.argv[1]).resolve()
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_FORCE_STDERR_LOGGING='1')
with TemporaryDirectory(prefix='bundle-native-') as folder:
    root = Path(folder)
    image = BytesIO(); Image.effect_noise((1200,900),90).convert('RGB').save(image,'PNG')
    originals = {'photo':image.getvalue(), 'video':b'original-video-bytes' * 10000}
    photos = []
    for pid, data in originals.items():
        path = root/pid; path.write_bytes(data)
        photos.append({'id':pid,'checkpoint':'EST.01','mime':'image/png' if pid=='photo' else 'video/mp4',
            'path':path.as_uri(), 'size':len(data),'digest':hashlib.sha256(data).hexdigest(),'uploaded':False})
    report = {'id':'local-report','charger_id':'charger','owner_id':'technician','revision':0,'status':'closed','dirty':True,
        '_conflict_remote':{'private':'must not export'}, '_inflight_payload':{'private':'must not export'},
        'snapshot':{'charger':{'external_id':'PC-001','model':'DC60','serial':'123'},'contract':{'name':'Contrato_Évora','company':'Cliente'}},
        'template':{'sections':[{'id':'EST','title':'Equipamento','items':[{'id':'EST.01','label':'Estado','kind':'status'}]}]},
        'payload':{'started':'2026-11-16T08:00:00Z','finished':'2026-11-16T09:15:30Z','answers':{'EST.01':{'value':'OK','notes':'Teste local'}}},
        'photos':photos, 'technician':'Técnico', 'presentation':{'issuer':'CSMTEC','revision':2,'client_logos':[]}}
    manifest, destination = root/'report.json', root/'result.zip'
    def run():
        manifest.write_text(json.dumps(report,ensure_ascii=False),encoding='utf-8')
        return subprocess.run([str(binary),str(manifest),str(destination),'zip'],env=env,capture_output=True)
    result = run(); assert result.returncode == 0, result.stderr
    with ZipFile(destination) as z:
        assert z.testzip() is None
        identifier = "PC-001_Contrato_Évora"
        doc = json.loads(z.read(identifier + ".json"))
        assert doc['sync_pending'] and doc['owner_id']=='technician' and doc['revision']==0
        assert '_conflict_remote' not in doc and '_inflight_payload' not in doc
        assert all('path' not in p and 'uploaded' not in p for p in doc['photos'])
        assert doc['identifier'] == identifier
        pdf = z.read(identifier+'.pdf')
        assert 'CSMTEC' in PdfReader(BytesIO(pdf)).pages[0].extract_text()
        for pid, extension, prefix in [('photo','png','fotografias'),('video','mp4','videos')]:
            name=f'{prefix}/EST.01/{identifier}_{pid}.{extension}'
            assert z.read(name)==originals[pid] and z.getinfo(name).compress_type==ZIP_STORED
    # Optional general observations and migrated historical notes must export
    # their media even when there is no answer text. Verify local PDF and ZIP.
    for checkpoint in ('SEG.07', 'SITE.06', 'QVE.14', 'EST.18', 'MAN.22', 'CARG.07', 'FIN.03', 'ANT.018'):
        report['template']['sections'][0]['items'].append(
            {'id': checkpoint, 'label': checkpoint + ' Observações gerais', 'kind': 'notes', 'optional': True})
        report['payload']['answers'][checkpoint] = None
        for original in photos[:2]:
            clone = dict(original, id=original['id'] + '-' + checkpoint.replace('.', '-'), checkpoint=checkpoint)
            report['photos'].append(clone)
    result = run(); assert result.returncode == 0, result.stderr
    with ZipFile(destination) as zipped:
        assert zipped.testzip() is None
        doc = json.loads(zipped.read(identifier + ".json"))
        pdf = PdfReader(BytesIO(zipped.read(doc['identifier'] + '.pdf')))
        text = '\n'.join(page.extract_text() for page in pdf.pages)
        drawn_photos = []
        for page in pdf.pages:
            resources = page['/Resources']['/XObject']
            def record_image(op, operands, cm, tm):
                if op == b'Do':
                    obj = resources[operands[0]]
                    if (obj.get('/Width'), obj.get('/Height')) == (1200, 900):
                        drawn_photos.append(operands[0])
            page.extract_text(visitor_operand_before=record_image)
        assert len(drawn_photos) == 9, 'Every optional-notes photograph must actually be drawn'
        assert text.count('Sem observações') == 8
        for media in doc['photos']:
            video = media['mime'].startswith('video/')
            folder, ext, original = ('videos', 'mp4', 'video') if video else ('fotografias', 'png', 'photo')
            name = f"{folder}/{media['checkpoint']}/{identifier}_{media['id']}.{ext}"
            assert zipped.read(name) == originals[original]
    # Every failure keeps an already chosen destination intact, with no partial ZIP.
    for invalid in ('digest','missing','path','draft'):
        destination.write_bytes(b'KEEP')
        backup = json.loads(json.dumps(report))
        if invalid=='digest': report['photos'][0]['digest']='bad'
        if invalid=='missing': report['photos'][0]['path']=(root/'missing').as_uri()
        if invalid=='path': report['photos'][0]['id']='../escape'
        if invalid=='draft': report['status']='draft'
        assert run().returncode != 0, invalid
        assert destination.read_bytes()==b'KEEP', invalid
        report=backup
print('PASS: offline ZIP, Unicode PDF name, JSON, exact photo/video originals, CRCs, issuer, private metadata removed; corrupt/missing/traversal/draft failures preserve destination')
