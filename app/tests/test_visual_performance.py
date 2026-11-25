# SPDX-License-Identifier: GPL-3.0-only
"""Exercise catalogue recycling, literal labels, progress bindings and preview teardown."""
from pathlib import Path
from shiboken6 import delete, getCppPointer

source = Path(__file__).with_name('test_report_tabs.py')
exec(compile(source.read_text().partition('def run_tests():')[0], str(source), 'exec'))
try:
    js('splashActive=false; page="catalog"; showFeedback=false')
    w.setWidth(390)
    w.setHeight(844)
    values['chargers'] = [dict(id=f'perf-{i}', external_id=f'PCVE-{i}',
        details=dict(address=f'<b>Rua {i}</b>', serial=str(i)), reports=[])
        for i in range(150)]
    lem.chargersChanged.emit()
    QTest.qWait(150)
    assert js('chargerList.count') == 150
    identities = {}
    def record_cards(item):
        if item.objectName().startswith("chargerCard_perf-"):
            identities.setdefault(getCppPointer(item)[0], set()).add(item.objectName())
        for child in item.childItems():
            record_cards(child)
    for index in [0, 40, 80, 120, 0]:
        js(f'chargerList.positionViewAtIndex({index}, ListView.Beginning)')
        QTest.qWait(100)
        card = find_visual(w.contentItem(), f'chargerCard_perf-{index}')
        assert card is not None
        record_cards(w.contentItem())
        label = find_visual(card, f'chargerAddress_perf-{index}')
        assert f'<b>Rua {index}</b>' in label.property('text')
        # QML enum comparison avoids Python enum conversion differences.
        assert js(f'chargerList.itemAtIndex({index}).modelData.id') == f'perf-{index}'
    # Inspect the common label type through its actual instantiated label.
    expression = QQmlExpression(QQmlEngine.contextForObject(label), label,
                               'textFormat === Text.PlainText')
    assert expression.evaluate()[0]
    assert js('chargerList.reuseItems')
    assert any(len(rows) > 1 for rows in identities.values()), 'No card was recycled'
    assert w.grabWindow().save(str(PREVIEW_DIR / 'visual-catalogue.png'))
    print('Catalogue screenshot:', PREVIEW_DIR / 'visual-catalogue.png')

    # Presence includes open drafts as well as closed reports in this contract.
    for index, status in enumerate(('draft', 'closed')):
        values['chargers'][index]['reports'] = [dict(id=f'report-{index}', status=status)]
    values['chargers'][0]['details']['address'] = 'Âncora São João · Service Bay'
    lem.chargersChanged.emit()
    QTest.qWait(100)
    js('chargerList.positionViewAtBeginning()')
    QTest.qWait(250)
    pointer_click('catalogReportFilterButton')
    assert js('catalogRows.map(c => c.id).join(",")') == 'perf-0,perf-1', js('catalogReportFilter')
    js('search.text="São João · Service"')
    QTest.qWait(250)
    assert js('catalogRows.map(c => c.id).join(",")') == 'perf-0'
    pointer_click('catalogReportFilterButton')
    assert js('chargerList.count') == 0
    values['chargers'][0]['reports'] = []
    lem.chargersChanged.emit()
    QTest.qWait(100)
    assert js('catalogRows.map(c => c.id).join(",")') == 'perf-0'
    pointer_click('clearCatalogSearch')
    QTest.qWait(250)
    assert js('chargerList.count') == 149
    pointer_click('catalogReportFilterButton')
    assert js('chargerList.count') == 150

    js('page="report"')
    before = js('totalAnswered()')
    item = next(i for s in values['sections'] for i in s['items']
                if not values['current']['payload']['answers'].get(i['id'], {}).get('value'))
    lem.answer(item['id'], 'OK', '')
    QTest.qWait(50)
    assert js('totalAnswered()') == before + 1
    assert js('totalItems()') == sum(len(s['items']) for s in values['sections'])
    fixture = PREVIEW_DIR / 'preview.png'
    image = QImage(64, 48, QImage.Format.Format_RGB32)
    image.fill(QColor('#00a6bb'))
    assert image.save(str(fixture))
    js('photoPreview.photoSource=' + json.dumps(QUrl.fromLocalFile(str(fixture)).toString()) + ';photoPreview.open()')
    QTest.qWait(100)
    js('photoPreview.close()')
    QTest.qWait(250)
    assert js('photoPreview.photoSource.toString()') == ''
    assert not errors, errors
    print('PASS: 150 catalogue rows, scroll/rebind, literal labels, reactive progress and preview teardown')
finally:
    delete(engine)
