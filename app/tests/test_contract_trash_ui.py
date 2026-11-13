# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""QML administrator confirmation and recovery controls, with mock backend."""
from pathlib import Path
from shiboken6 import delete
source = Path(__file__).with_name("test_report_tabs.py")
prefix = source.read_text().partition("def run_tests():")[0]
prefix = prefix.replace('values["photosByCheckpoint"] = {}',
                        'values["photosByCheckpoint"] = {}\nvalues["deletedContracts"] = []')
prefix = prefix.replace('Lem = type("Lem", (QObject,), attrs)', '''
contract_calls=[]
def deleteContract(self, cid, confirmation):
    contract_calls.append(("delete",cid,confirmation))
def loadDeletedContracts(self):
    values["deletedContracts"]=[{"id":"removed", "name":"2026_1ºS_Test_TRE", "purge_after":"2026-12-13T12:00:00+00:00"}]
    self.changed.emit()
def restoreContract(self,cid):
    contract_calls.append(("restore",cid))
    values["deletedContracts"]=[]
    self.changed.emit()
attrs["deleteContract"]=Slot(str,str)(deleteContract)
attrs["loadDeletedContracts"]=Slot()(loadDeletedContracts)
attrs["restoreContract"]=Slot(str)(restoreContract)
Lem = type("Lem", (QObject,), attrs)
''')
exec(compile(prefix,str(source),"exec"), globals())
values["authenticated"]=True
lem.changed.emit()
js('splashActive=false; page="admin"')
for width,height in [(390,844),(1050,850),(640,390)]:
    w.setWidth(width); w.setHeight(height)
    QTest.qWait(80)
    trigger("deleteContractButton")
    confirm=find_visual(w.contentItem(),"confirmDeleteContract")
    field=find_visual(w.contentItem(),"deleteContractConfirmation")
    assert not confirm.property("enabled")
    for word in ["apagar","APAGA","APAGAR "," APAGAR"]:
        field.setProperty("text",word)
        assert not confirm.property("enabled")
    field.setProperty("text","APAGAR")
    assert confirm.property("enabled")
    trigger("confirmDeleteContract")
    assert contract_calls[-1]==("delete","contract","APAGAR")
    assert js('page')=="admin"
    trigger("deleteContractButton")
    assert field.property("text")=="", "Every confirmation must start blank"
    js('deleteContractDialog.close()')
    trigger("deletedContractsButton")
    trigger("restoreContract_removed")
    assert contract_calls[-1]==("restore","removed")
    js('deletedContractsDialog.close()')
values["user"]["role"]="tech"
lem.changed.emit();QTest.qWait(30)
assert not find_visual(w.contentItem(),"deleteContractButton").isVisible()
assert not find_visual(w.contentItem(),"deletedContractsButton").isVisible()
assert not errors,errors
print("PASS: admin-only controls; exact APAGAR; reset confirmation; recovery list/actions; desktop/mobile/landscape; no QML warnings")
delete(engine)
