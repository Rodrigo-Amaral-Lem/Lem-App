# SPDX-License-Identifier: GPL-3.0-only
"""Compare real native/server PDFs with identical synthetic fixtures and Poppler.

python test_pdf_parity.py path/to/Lem-pdf-parity output-directory
Requires server dependencies, Pillow, pypdf and pdftoppm on PATH.
Run on both Windows and Linux with the deployed Qt/PySide versions.
"""
import json
import os
import re
import subprocess
import sys
from io import BytesIO
from pathlib import Path
from types import SimpleNamespace
from PIL import Image, ImageChops, ImageStat
from pypdf import PdfReader

workspace = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(workspace / 'Server/server'))
from app.pdf_report import build_pdf
from app.branding import selection

native, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
output.mkdir(parents=True, exist_ok=True)
image = Image.new('RGB', (300, 200), '#2689aa')
photo_path = output / 'synthetic-evidence.png'
image.save(photo_path)
photo = SimpleNamespace(id='photo', checkpoint='EST.01', mime='image/png', data=photo_path.read_bytes())
report = {'id':'test', 'revision':2, 'status':'closed', 'technician':'Técnico de teste',
    'started':'2026-07-15T08:00:00Z', 'finished':'2026-07-15T09:15:30Z',
    'snapshot': {'charger': {'external_id':'PCVE-001', 'address':'Avenida da Liberdade, Lisboa',
        'manufacturer':'Exemplo', 'model':'DC60', 'serial':'000012', 'power':'94', 'power_limit':0,
        'connectors':'2x Mennekes Type 2'},
        'contract': {'name':'2026_2ºS_Cliente_TRE', 'company':'Cliente de exemplo'}},
    'template': {'version': 7, 'sections': [
        {'id':'EST','title':'EST — Equipamento','items':[{'id':'EST.01','label':'Verificar o estado do equipamento','kind':'status'}]},
        {'id':'MAN','title':'MAN — Medições','items':[{'id':'MAN.04','label':'Tensão entre fases (360-440 V)','kind':'text'},
            {'id':'MAN.11','label':'Calibre configurado','kind':'text'},
            {'id':'MAN.12','label':'Intensidade de disparo medida','kind':'text'},
            {'id':'MAN.18','label':'Valor da resistência da terra','kind':'text'}]},
        {'id':'CARG','title':'CARG — Ensaios de carga','items':[
            {'id':'CARG.01.01','label':'Teste de carga do conector Mennekes Type 2 #1','kind':'status'},
            {'id':'CARG.01.02','label':'Teste de carga do conector Mennekes Type 2 #2','kind':'status'}]},
        {'id':'FIN','title':'FIN — Conclusão','items':[{'id':'FIN.03','label':'Observações gerais finais','kind':'notes'}]}]},
    'payload': {'answers': {'EST.01':{'value':'OK','notes':'Equipamento em boas condições.'},
        'MAN.04':{'value':'359,99'}, 'MAN.11':{'value':'300'},
        'MAN.12':{'value':'315','assessment':'NOK','notes':'Valor de intensidade de disparo diferencial fora do calibre configurado'},
        'MAN.18':{'value':'167','assessment':'NOK','notes':'Valor de terra de proteção elevado'}, 'FIN.03':{'notes':('Observações com acentuação e texto longo. ' * 90) + ' FIM DO TEXTO'}}},
    'photos':[{'id':'photo','checkpoint':'EST.01','mime':'image/png','path':photo_path.as_uri()}]}
env = dict(os.environ, QT_QPA_PLATFORM=os.environ.get('LEM_NATIVE_PDF_PLATFORM','offscreen'), QT_QUICK_BACKEND='software', QT_FORCE_STDERR_LOGGING='1')
for issuer in ('Lem','CSMTEC'):
    report['presentation'] = {'issuer':issuer,'revision':1,'client_logos':selection(['APS','Galp'])}
    manifest = output / (issuer + '.json')
    manifest.write_text(json.dumps(report, ensure_ascii=False), encoding='utf-8')
    server_pdf, native_pdf = output / (issuer + '-server.pdf'), output / (issuer + '-app.pdf')
    server_pdf.write_bytes(build_pdf(report, [photo]))
    subprocess.run([str(native), str(manifest), str(native_pdf)], env=env, check=True)
    server_reader, native_reader = PdfReader(server_pdf), PdfReader(native_pdf)
    assert len(server_reader.pages) == len(native_reader.pages)
    for a, b in zip(server_reader.pages, native_reader.pages):
        assert a.extract_text() == b.extract_text()
        # Font object numbers are private PDF IDs. Every drawing instruction,
        # glyph advance, baseline, image rectangle and page break must agree.
        assert re.sub(rb'/F\d+', b'/F', a.get_contents().get_data()) == re.sub(rb'/F\d+', b'/F', b.get_contents().get_data())
    exported_text = ''.join(p.extract_text() for p in server_reader.pages)
    assert 'FIM DO TEXTO' in exported_text
    assert 'Mennekes' not in exported_text
    assert '2x AC Type 2' in exported_text and 'AC Type 2 #1' in exported_text and 'AC Type 2 #2' in exported_text
    assert report['snapshot']['charger']['connectors'] == '2x Mennekes Type 2'
    assert '300 mA' in exported_text and '315 mA' in exported_text
    assert 'Avaliação: NOK' in exported_text
    for name, pdf in [('server',server_pdf),('app',native_pdf)]:
        subprocess.run(['pdftoppm','-r','96','-png',str(pdf),str(output/(issuer+'-'+name))], check=True, capture_output=True)
    pages = sorted(output.glob(issuer+'-server-*.png'))
    for server_image in pages:
        native_image = output / server_image.name.replace('-server-','-app-')
        with Image.open(server_image) as a, Image.open(native_image) as b:
            assert a.size == b.size
            difference = ImageChops.difference(a.convert('RGB'), b.convert('RGB'))
            # Windows GUI/offscreen font subsets cause tiny antialiasing changes
            # at a few glyph-edge pixels. Geometry is checked exactly above.
            maximum = max(high for low, high in difference.getextrema())
            assert maximum <= 16 and max(ImageStat.Stat(difference).mean) < 0.01, (issuer, server_image.name, maximum)
    print(f'PASS {issuer}: {len(pages)} pages, identical text and drawing coordinates; visual difference < 0.01/255 average')
