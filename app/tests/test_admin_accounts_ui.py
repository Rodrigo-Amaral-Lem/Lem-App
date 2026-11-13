"""Management roles and session-only diagnostic reveal with real QML controls."""
from pathlib import Path
from shiboken6 import delete
source=Path(__file__).with_name("test_report_tabs.py")
prefix=source.read_text().partition("def run_tests():")[0]
prefix=prefix.replace('Lem = type("Lem", (QObject,), attrs)', '''
created_accounts=[]
def createUser(self,email,name,password,role):
    created_accounts.append((email,name,password,role))
def useDefaultServer(self):
    pass
attrs["createUser"]=Slot(str,str,str,str)(createUser)
attrs["useDefaultServer"]=Slot()(useDefaultServer)
Lem = type("Lem", (QObject,), attrs)
''')
exec(compile(prefix,str(source),"exec"),globals())
values["authenticated"]=True;lem.changed.emit()
js('splashActive=false;page="admin";adminArea.currentIndex=2')
for width,height in [(320,700),(390,844),(1050,850),(640,390)]:
    w.setWidth(width);w.setHeight(height);QTest.qWait(80)
    for key in ["newName","newEmail","newPassword","newUserRole"]:
        field=find_visual(w.contentItem(),key)
        assert field.mapToScene(QPoint(int(field.width()),0)).x()<=w.width()-js("pageInset")+1,(key,width,field.width())
    for key,value in [("newName","New Admin"),("newEmail","admin2@lem.pt"),("newPassword","StrongPassword123!")]:
        find_visual(w.contentItem(),key).setProperty("text",value)
    role=find_visual(w.contentItem(),"newUserRole");role.setProperty("currentIndex",1)
    trigger("createUserButton")
    assert created_accounts[-1][-1]=="admin"
    assert find_visual(w.contentItem(),"newPassword").property("text")==""
js('loginDialog.open()');QTest.qWait(100)
assert not find_visual(w.contentItem(),"openScrollDiagnosticButton").isVisible()
assert not find_visual(w.contentItem(),"localDemoButton").isVisible()
for _ in range(9):trigger("defaultServerButton")
assert not js("debugToolsEnabled")
trigger("defaultServerButton")
assert js("debugToolsEnabled")
assert find_visual(w.contentItem(),"openScrollDiagnosticButton").isVisible()
assert find_visual(w.contentItem(),"localDemoButton").isVisible()
for _ in range(10):trigger("defaultServerButton")
assert not js("debugToolsEnabled")
# Closing midway cancels the sequence without enabling it.
for _ in range(5):trigger("defaultServerButton")
js('loginDialog.close();loginDialog.open()');QTest.qWait(100)
assert js('debugTapCount')==0
js('loginDialog.close();adminArea.currentIndex=2;page="admin";adminScroll.contentY=0')
w.setWidth(390);w.setHeight(844);QTest.qWait(150)
assert w.grabWindow().save(str(PREVIEW_DIR/'management-accounts.png'))
js('loginDialog.open()');QTest.qWait(150)
assert w.grabWindow().save(str(PREVIEW_DIR/'account.png'))
print('Preview:',str(PREVIEW_DIR))
assert not errors,errors
print('PASS: administrator creation role, password cleared, separated management areas, 10 taps on/off and default-hidden tools across desktop/mobile sizes')
delete(engine)
