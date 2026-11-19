// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

// Lem presentation layer. The context property `lem` is the C++ Lem instance.
// This file owns control styling, navigation and temporary dialog/section state;
// authoritative report persistence, permissions and network work stay in C++/the API.
// Reading map: shared controls -> view helpers -> header/menus -> dialogs -> pages.
// See docs/developer/qml-ui.md before changing delegates or notification bindings.

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Controls.Basic as Base
import QtQuick.Layouts
import QtQuick.Dialogs
import "Measurements.js" as Measurements

ApplicationWindow {
    id: win
    width: 1100
    height: 820
    visible: true
    title: "Lem"
    color: win.pageSurface
    palette: lightPalette
    font.pixelSize: 16

    // Cyan comes from the logo; the darker action shade keeps white labels readable.
    // Status colours are independent of branding and always accompanied by text.
    readonly property color brandPrimary: "#007c91"
    readonly property color brandAccent: "#00a6bb"
    readonly property color brandTint: "#e3f4f7"
    readonly property color brandTrack: "#a5d7df"
    readonly property color pageSurface: "#f4f6f7"
    readonly property color softSurface: "#edf2f4"
    readonly property color ink: "#263238"
    readonly property color mutedInk: "#52616a"
    readonly property color outline: "#71828b"
    readonly property color cardOutline: "#c7d3d9"
    readonly property color disabledSurface: "#e3e9ec"
    readonly property color hoverSurface: "#e3f1f4"
    readonly property color pressedSurface: "#cce6eb"
    readonly property color success: "#256b48"
    readonly property color danger: "#a12626"
    readonly property color neutralAnswer: "#52616a"

    readonly property string interfaceRevision: "bounds032-safearea1"
    property string capturedPageDiagnostic: ""
    property bool diagnosticCopied: false
    property bool debugToolsEnabled: false
    property int debugTapCount: 0
    Timer {
        id: debugTapTimer
        interval: 5000
        onTriggered: win.debugTapCount = 0
    }
    function recordDebugTap() {
        debugTapTimer.restart();
        debugTapCount++;
        if (debugTapCount === 10) {
            debugToolsEnabled = !debugToolsEnabled;
            debugTapCount = 0;
            debugTapTimer.stop();
        }
    }
    function diagnosticBox(item, target) {
        if (!item)
            return null;
        const point = item.mapToItem(target || win.contentItem, 0, 0);
        return {
            x: point.x,
            y: point.y,
            width: item.width,
            height: item.height,
            implicitHeight: item.implicitHeight,
            visible: item.visible,
            opacity: item.opacity
        };
    }
    function capturePageDiagnostic() {
        const view = activePageScroll;
        const result = {
            appVersion: Qt.application.version,
            interfaceRevision: interfaceRevision,
            qtRuntime: typeof qtRuntimeVersion !== "undefined" ? qtRuntimeVersion : "test-runtime",
            platform: Qt.platform.os,
            inputMethodVisible: Qt.inputMethod.visible,
            keyboardHeight: Qt.inputMethod.keyboardRectangle.height,
            busy: lem.busy,
            syncing: lem.syncing,
            page: page,
            selectedSection: selectedSection,
            window: {
                width: width,
                height: height,
                contentHeight: win.contentItem.height,
                screenWidth: Screen.width,
                screenHeight: Screen.height,
                devicePixelRatio: Screen.devicePixelRatio
            },
            chrome: {
                hidden: chromeHidden,
                progress: chromeProgress,
                pageChromeHeight: pageChromeHeight,
                pageInset: pageInset,
                pageBottomInset: pageBottomInset,
                pageGap: pageGap
            },
            footer: diagnosticBox(appFooter),
            viewport: diagnosticBox(view)
        };
        if (view) {
            result.scroll = {};
            for (const key of ["contentY", "contentHeight", "contentWidth", "originY", "topMargin", "bottomMargin", "verticalOvershoot", "atYBeginning", "atYEnd", "moving", "flicking", "dragging", "boundsBehavior", "boundsMovement"])
                result.scroll[key] = view[key] === undefined ? null : view[key];
            result.scroll.type = String(view);
            result.scroll.renderedEnd = pageAtRenderedEnd();
        }
        let last = null;
        if (page === "report") {
            const panel = sectionPanels.itemAt(selectedSection);
            if (panel && panel.checkpoints && panel.checkpoints.count)
                last = panel.checkpoints.itemAt(panel.checkpoints.count - 1);
            result.panelCount = sectionPanels.count;
            result.checkpointCount = panel && panel.checkpoints ? panel.checkpoints.count : 0;
            result.panel = diagnosticBox(panel, view ? view.contentItem : null);
            result.body = diagnosticBox(reportBody, view ? view.contentItem : null);
            result.body.childrenBottom = reportBody.childrenRect.y + reportBody.childrenRect.height;
            result.panelComputedBottom = panel ? panel.y + panel.lastCheckpointBottom + pageBottomInset : null;
        } else if (page === "catalog") {
            result.itemCount = chargerList.count;
            last = chargerList.count ? chargerList.itemAtIndex(chargerList.count - 1) : null;
            result.catalogueFooter = diagnosticBox(chargerList.footerItem, chargerList.contentItem);
        } else {
            result.body = diagnosticBox(adminBody, adminScroll.contentItem);
            result.adminTail = diagnosticBox(adminTail, adminScroll.contentItem);
        }
        result.lastCard = diagnosticBox(last);
        if (last && view) {
            result.lastCard.objectName = last.objectName;
            result.lastCard.inContent = diagnosticBox(last, view.contentItem);
            result.lastCard.inViewport = diagnosticBox(last, view);
            result.lastCard.blankBelowInViewport = view.height - result.lastCard.inViewport.y - last.height;
            result.lastCard.mappedContentEnd = result.lastCard.inContent.y + last.height + pageBottomInset;
        }
        capturedPageDiagnostic = JSON.stringify(result, null, 2);
        diagnosticCopied = false;
    }
    function openAccount() {
        // Snapshot before a modal changes focus/interaction; never include report/user data.
        capturePageDiagnostic();
        loginDialog.open();
    }
    property bool splashActive: true
    readonly property var reportBrandCatalogue: lem.reportBrands()
    property url logoSource: "qrc:/branding/lem-logo-tight.svg"
    // One random draw per process: mutually exclusive 5% / 5% / 1% / 89%.
    // Keep the draw separate so boundary cases can be exercised deterministically.
    property real startupDraw: Math.random()
    function startupVariant(draw) {
        return draw < 0.05 ? 1 : draw < 0.10 ? 2 : draw < 0.11 ? 3 : 0;
    }
    readonly property int startupChoice: startupVariant(startupDraw)
    property bool startupAnimationFailed: false
    readonly property bool startupUsesAnimation: startupChoice !== 0 && !startupAnimationFailed
    readonly property string startupAnimationName: startupChoice === 1 ? "greeting" : startupChoice === 2 ? "checklist" : "charging"
    // An in-scene overlay cannot close a platform popup/window at the splash boundary.
    FocusScope {
        id: startupSplash
        objectName: "startupSplash"
        parent: Overlay.overlay
        anchors.fill: parent
        z: 10000
        visible: win.splashActive
        focus: visible
        onVisibleChanged: {
            if (visible)
                forceActiveFocus();
            else
                win.contentItem.forceActiveFocus();
        }
        Keys.onPressed: event => {
            event.accepted = true;
        }
        Rectangle {
            anchors.fill: parent
            // The static mark is transparent, so give it a white canvas.
            color: win.startupUsesAnimation ? win.pageSurface : "#ffffff"
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: wheel => {
                wheel.accepted = true;
            }
        }
        Image {
            id: startupLogo
            objectName: "startupLogo"
            anchors.centerIn: parent
            width: parent.width * 0.8
            height: parent.height * 0.8
            visible: !win.startupUsesAnimation
            source: visible ? win.logoSource : ""
            fillMode: Image.PreserveAspectFit
            asynchronous: false
            cache: true
            Accessible.name: "Logótipo LEM"
        }
        Item {
            id: startupAnimation
            objectName: "startupAnimation"
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height, 512)
            height: width
            visible: win.startupUsesAnimation
            // Ordinary PNG textures: no QMovie/GIF decoder or animated-image teardown.
            readonly property var frameOrder: win.startupChoice === 1 ? [0, 1, 2, 3, 2, 1, 0] : [0, 1, 2, 3, 4, 5]
            readonly property var frameDurations: win.startupChoice === 1 ? [240, 200, 200, 500, 200, 200, 460] : win.startupChoice === 2 ? [300, 220, 220, 320, 340, 1000] : [300, 240, 240, 260, 360, 1000]
            property int currentFrame: 0
            readonly property int frameCount: frameOrder.length
            readonly property bool framesReady: {
                if (startupFrames.count !== 6)
                    return false;
                for (let i = 0; i < 6; ++i) {
                    const frame = startupFrames.itemAt(i);
                    if (!frame || frame.status !== Image.Ready)
                        return false;
                }
                return true;
            }
            readonly property int status: framesReady ? Image.Ready : Image.Loading
            readonly property bool playing: win.splashActive && visible && framesReady
            onPlayingChanged: if (playing)
                currentFrame = 0
            Accessible.name: "Animação de boas-vindas Lem"
            Repeater {
                id: startupFrames
                model: 6
                Image {
                    required property int index
                    anchors.fill: parent
                    source: win.splashActive && win.startupUsesAnimation ? "qrc:/startup/" + win.startupAnimationName + "-" + index + ".png" : ""
                    visible: startupAnimation.framesReady && startupAnimation.frameOrder[startupAnimation.currentFrame] === index
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: true
                    onStatusChanged: if (status === Image.Error)
                        win.startupAnimationFailed = true
                }
            }
            Timer {
                interval: startupAnimation.frameDurations[startupAnimation.currentFrame] || 240
                running: startupAnimation.playing
                repeat: true
                onTriggered: {
                    if (startupAnimation.currentFrame + 1 < startupAnimation.frameCount)
                        ++startupAnimation.currentFrame;
                    else
                        win.splashActive = false;
                }
            }
        }
    }
    Timer {
        id: splashHold
        interval: 2000
        running: win.splashActive && !win.startupUsesAnimation
        repeat: false
        onTriggered: win.splashActive = false
    }
    Timer {
        // Decoding must never trap the user on an empty splash.
        interval: 3000
        running: win.splashActive && win.startupUsesAnimation && !startupAnimation.framesReady
        onTriggered: win.startupAnimationFailed = true
    }

    // One palette and one set of controls for every platform. Native file
    // dialogs retain their system theme; app-owned surfaces use this palette.
    Palette {
        id: lightPalette
        window: win.pageSurface
        windowText: win.ink
        base: "#ffffff"
        alternateBase: win.softSurface
        text: win.ink
        button: win.softSurface
        buttonText: win.ink
        placeholderText: win.mutedInk
        highlight: win.brandPrimary
        highlightedText: "#ffffff"
        accent: win.brandPrimary
        mid: win.outline
        dark: win.mutedInk
        light: "#ffffff"
        midlight: win.softSurface
        shadow: win.mutedInk
        toolTipBase: "#ffffff"
        toolTipText: win.ink
        link: win.brandPrimary
        linkVisited: "#514d82"
        brightText: "#ffffff"
    }
    // Cache the QVariant projection for helpers; repeated property conversion is costly in delegates.
    // One projection of the active report per notification. Bindings read this cached
    // JS object: every direct read of lem.current converts the whole report again
    // (answers, checklist and snapshot). Handlers that have just called a lem action
    // may still read lem.current directly.
    property var reportData: lem.current
    // Photo delegates depend only on photo changes, never answer/status notifications.
    property var reportPhotos: lem.photosByCheckpoint
    property bool networkActivity: lem.busy || lem.syncing || lem.refreshing || lem.downloadingPhotos
    property bool reportNetworkActivity: lem.busy || lem.syncing || lem.refreshing || !!lem.currentReportDownloadingPhotos

    property bool compact: width <= 640 || (androidCamera && height <= 480)
    property real touchHeight: (androidCamera || compact) ? 48 : 44
    property bool androidCamera: Qt.platform.os === "android"
    // Android page chrome responds only to deliberate vertical page movement.
    // Dialog scrolling, group restoration and viewport resizing must not fake a gesture.
    property bool autoHideChrome: Qt.platform.os === "android"
    property real chromeReveal: 1
    readonly property bool chromeHidden: chromeProgress <= .001
    readonly property real chromeProgress: autoHideChrome && !chromeAtTop ? chromeReveal : 1
    readonly property var activeChargerList: page === "catalog" ? chargerList : page === "pcves" ? pcveList : null
    readonly property var activePageScroll: page === "report" ? reportScroll : activeChargerList || adminScroll
    readonly property bool chromeAtTop: !activePageScroll || activePageScroll.atYBeginning || activePageScroll.contentY <= pageScrollOrigin() + 2
    property real lastPageScrollY: 0
    // One full collapse takes roughly one toolbar stack's travel. While the
    // page moves, follow its displacement directly rather than chasing an
    // animation on every frame (which introduces lag and jumps on reversal).
    readonly property real chromeTravel: Math.max(96, pageTopInset, appFooter.height)
    Behavior on chromeReveal {
        enabled: !win.activePageScroll || !win.activePageScroll.moving
        NumberAnimation {
            id: chromeAnimation
            duration: 200
            easing.type: Easing.OutCubic
        }
    }
    function pageScrollOrigin() {
        if (!activePageScroll)
            return 0;
        const header = activeChargerList && activeChargerList.headerItem;
        return (activePageScroll.originY || 0) - (header ? header.height : 0);
    }
    function pageScrollOffset() {
        if (!activePageScroll || activePageScroll.contentY === undefined)
            return 0;
        const origin = pageScrollOrigin();
        const maximum = Math.max(0, activePageScroll.contentHeight - activePageScroll.height);
        return Math.max(0, Math.min(activePageScroll.contentY - origin, maximum));
    }
    function resetScrollChrome() {
        chromeReveal = 1;
        lastPageScrollY = pageScrollOffset();
    }
    property bool trimmingCatalogueTail: false
    function trimCatalogueTail() {
        const list = activeChargerList;
        if (!list || trimmingCatalogueTail || list.count === 0)
            return;
        const last = list.itemAtIndex(list.count - 1);
        if (!last)
            return;
        const maximum = Math.max(list.originY, last.y + last.height + pageBottomInset - list.height);
        if (list.contentY > maximum + .5) {
            trimmingCatalogueTail = true;
            list.cancelFlick();
            list.contentY = maximum;
            trimmingCatalogueTail = false;
        }
    }
    function pageAtRenderedEnd() {
        if (!activePageScroll)
            return true;
        const list = activeChargerList;
        if (list && list.count > 0) {
            const last = list.itemAtIndex(list.count - 1);
            if (last)
                return list.contentY >= last.y + last.height + pageBottomInset - list.height - .5;
        }
        return activePageScroll.atYEnd;
    }
    function observePageScroll() {
        const y = pageScrollOffset(), delta = y - lastPageScrollY;
        lastPageScrollY = y;
        if (!autoHideChrome || chromeAtTop) {
            resetScrollChrome();
            return;
        }
        if (!activePageScroll.moving) {
            return;
        }
        if (Math.abs(delta) < .01)
            return;
        chromeReveal = Math.max(0, Math.min(1, chromeReveal - delta / chromeTravel));
    }
    onAutoHideChromeChanged: resetScrollChrome()
    onActivePageScrollChanged: resetScrollChrome()
    onChromeAtTopChanged: if (chromeAtTop)
        resetScrollChrome()
    Connections {
        target: win.activePageScroll
        ignoreUnknownSignals: true
        function onContentYChanged() {
            if (win.trimmingCatalogueTail)
                return;
            win.trimCatalogueTail();
            win.observePageScroll();
        }
        function onHeightChanged() {
            win.trimCatalogueTail();
            win.lastPageScrollY = win.pageScrollOffset();
        }
        function onContentHeightChanged() {
            win.trimCatalogueTail();
            win.lastPageScrollY = win.pageScrollOffset();
        }
        function onMovementEnded() {
            win.lastPageScrollY = win.pageScrollOffset();
        }
    }
    // Overlay chrome slides without participating in scroll viewport measurement.
    // Insets in the scrollable content stay constant, so thumb size and questions do not jump.
    component ScrollChrome: Item {
        id: chromePanel
        default property alias contents: chromeLayout.data
        property alias contentSpacing: chromeLayout.spacing
        implicitHeight: chromeLayout.implicitHeight
        width: parent.width
        height: implicitHeight
        y: win.pageInset
        z: 20
        transform: Translate {
            y: -win.pageTopInset * (1 - win.chromeProgress)
        }
        // Slide as an opaque surface: fading would show question text
        // through partially revealed controls and make them hard to read.
        opacity: win.chromeProgress > .001 ? 1 : 0
        clip: true
        enabled: win.chromeProgress > .98
        ColumnLayout {
            id: chromeLayout
            width: parent.width
            spacing: 8
        }
    }
    component ChromeBackdrop: Rectangle {
        id: chromeBackdrop
        width: parent.width
        height: win.pageTopInset
        color: win.color
        z: 10
        // Slide as an opaque surface: fading would show question text
        // through partially revealed controls and make them hard to read.
        opacity: win.chromeProgress > .001 ? 1 : 0
        transform: Translate {
            y: -chromeBackdrop.height * (1 - win.chromeProgress)
        }
    }
    // One fixed end inset: only the footer overlap plus a small gap. It must not
    // animate with chromeProgress, or the reading position/scroll thumb would drift.
    readonly property real pageInset: win.compact ? 12 : 16
    readonly property real pageTopInset: pageInset + pageChromeHeight + pageGap
    readonly property real pageBottomInset: appFooter.height + pageGap
    // Preserve desktop keyboard scrolling after removing automatic ScrollView wrappers.
    function scrollPageKey(view, event) {
        let target = view.contentY;
        switch (event.key) {
        case Qt.Key_Down:
            target += 40;
            break;
        case Qt.Key_Up:
            target -= 40;
            break;
        case Qt.Key_PageDown:
            target += view.height * .9;
            break;
        case Qt.Key_PageUp:
            target -= view.height * .9;
            break;
        case Qt.Key_Home:
            target = 0;
            break;
        case Qt.Key_End:
            target = view.contentHeight;
            break;
        default:
            return;
        }
        view.cancelFlick();
        view.contentY = Math.max(0, Math.min(target, view.contentHeight - view.height));
        event.accepted = true;
    }
    readonly property real pageGap: win.compact ? 8 : 12
    readonly property real pageChromeHeight: {
        if (page === "report")
            return reportChrome.implicitHeight;
        if (page === "pcves")
            return pcveChrome.implicitHeight;
        return contractChrome.implicitHeight + (page === "catalog" ? pageGap + catalogChrome.implicitHeight : 0);
    }
    // Geometric icons do not depend on platform fonts or Unicode glyph coverage.
    component UiIcon: Canvas {
        property string kind: "down"
        property color ink: win.ink
        implicitWidth: 20
        implicitHeight: 20
        antialiasing: true
        onKindChanged: requestPaint()
        onInkChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const c = getContext("2d");
            c.reset();
            c.strokeStyle = ink;
            c.lineWidth = 2;
            c.lineCap = "round";
            c.lineJoin = "round";
            if (kind === "erase") {
                c.beginPath();
                c.moveTo(width * .16, height * .61);
                c.lineTo(width * .57, height * .18);
                c.lineTo(width * .88, height * .46);
                c.lineTo(width * .54, height * .82);
                c.lineTo(width * .36, height * .82);
                c.closePath();
                c.stroke();
                c.beginPath();
                c.moveTo(width * .37, height * .39);
                c.lineTo(width * .67, height * .68);
                c.moveTo(width * .51, height * .85);
                c.lineTo(width * .92, height * .85);
                c.stroke();
            } else if (kind === "navigate") {
                c.beginPath();
                c.moveTo(width * .83, height * .14);
                c.lineTo(width * .59, height * .86);
                c.lineTo(width * .43, height * .57);
                c.lineTo(width * .14, height * .41);
                c.closePath();
                c.stroke();
            } else if (kind === "sync") {
                const cx = width / 2, cy = height / 2, r = Math.min(width, height) * .31;
                for (let start of [-Math.PI * .85, Math.PI * .15]) {
                    const end = start + Math.PI * .76;
                    c.beginPath();
                    c.arc(cx, cy, r, start, end);
                    c.stroke();
                    const x = cx + r * Math.cos(end), y = cy + r * Math.sin(end);
                    const tx = -Math.sin(end), ty = Math.cos(end), arrow = width * .18;
                    c.beginPath();
                    c.moveTo(x - tx * arrow + Math.cos(end) * arrow * .65, y - ty * arrow + Math.sin(end) * arrow * .65);
                    c.lineTo(x, y);
                    c.lineTo(x - tx * arrow - Math.cos(end) * arrow * .65, y - ty * arrow - Math.sin(end) * arrow * .65);
                    c.stroke();
                }
            } else if (kind === "menu") {
                for (let y of [0.25, 0.5, 0.75]) {
                    c.beginPath();
                    c.moveTo(width * 0.15, height * y);
                    c.lineTo(width * 0.85, height * y);
                    c.stroke();
                }
            } else if (kind === "account") {
                c.beginPath();
                c.arc(width * .5, height * .3, width * .17, 0, Math.PI * 2);
                c.stroke();
                c.beginPath();
                c.moveTo(width * .15, height * .88);
                c.bezierCurveTo(width * .16, height * .48, width * .84, height * .48, width * .85, height * .88);
                c.stroke();
            } else if (kind === "check") {
                c.beginPath();
                c.moveTo(width * .18, height * .51);
                c.lineTo(width * .4, height * .73);
                c.lineTo(width * .83, height * .25);
                c.stroke();
            } else if (kind === "warning") {
                c.beginPath();
                c.moveTo(width * .5, height * .18);
                c.lineTo(width * .5, height * .58);
                c.stroke();
                c.fillStyle = ink;
                c.beginPath();
                c.arc(width * .5, height * .81, width * .055, 0, Math.PI * 2);
                c.fill();
            } else if (kind === "demo") {
                c.beginPath();
                c.moveTo(width * .3, height * .17);
                c.lineTo(width * .78, height * .5);
                c.lineTo(width * .3, height * .83);
                c.closePath();
                c.stroke();
            } else if (kind === "zoom") {
                c.beginPath();
                c.arc(width * .42, height * .4, width * .27, 0, Math.PI * 2);
                c.stroke();
                c.beginPath();
                c.moveTo(width * .62, height * .61);
                c.lineTo(width * .88, height * .87);
                c.moveTo(width * .29, height * .4);
                c.lineTo(width * .55, height * .4);
                c.moveTo(width * .42, height * .27);
                c.lineTo(width * .42, height * .53);
                c.stroke();
            } else if (kind === "trash") {
                c.beginPath();
                c.moveTo(width * .16, height * .28);
                c.lineTo(width * .84, height * .28);
                c.moveTo(width * .36, height * .28);
                c.lineTo(width * .36, height * .14);
                c.lineTo(width * .64, height * .14);
                c.lineTo(width * .64, height * .28);
                c.moveTo(width * .25, height * .28);
                c.lineTo(width * .3, height * .87);
                c.lineTo(width * .7, height * .87);
                c.lineTo(width * .75, height * .28);
                c.moveTo(width * .43, height * .41);
                c.lineTo(width * .43, height * .72);
                c.moveTo(width * .57, height * .41);
                c.lineTo(width * .57, height * .72);
                c.stroke();
            } else if (kind === "download") {
                c.beginPath();
                c.moveTo(width * .5, height * .13);
                c.lineTo(width * .5, height * .65);
                c.moveTo(width * .3, height * .46);
                c.lineTo(width * .5, height * .66);
                c.lineTo(width * .7, height * .46);
                c.moveTo(width * .17, height * .7);
                c.lineTo(width * .17, height * .87);
                c.lineTo(width * .83, height * .87);
                c.lineTo(width * .83, height * .7);
                c.stroke();
            } else if (kind === "close") {
                c.beginPath();
                c.moveTo(width * .24, height * .24);
                c.lineTo(width * .76, height * .76);
                c.moveTo(width * .76, height * .24);
                c.lineTo(width * .24, height * .76);
                c.stroke();
            } else if (kind === "camera") {
                c.beginPath();
                c.moveTo(width * .12, height * .3);
                c.lineTo(width * .32, height * .3);
                c.lineTo(width * .4, height * .16);
                c.lineTo(width * .64, height * .16);
                c.lineTo(width * .72, height * .3);
                c.lineTo(width * .88, height * .3);
                c.lineTo(width * .88, height * .82);
                c.lineTo(width * .12, height * .82);
                c.closePath();
                c.stroke();
                c.beginPath();
                c.arc(width * .5, height * .55, width * .17, 0, Math.PI * 2);
                c.stroke();
            } else if (kind === "video") {
                c.strokeRect(width*.08, height*.22, width*.57, height*.56);
                c.beginPath(); c.moveTo(width*.65,height*.35); c.lineTo(width*.95,height*.18); c.lineTo(width*.95,height*.82); c.lineTo(width*.65,height*.65); c.stroke();
            } else if (kind === "photoEdit") {
                // Photograph frame plus pencil: this action edits more than crop.
                c.beginPath();
                c.moveTo(width*.43,height*.86); c.lineTo(width*.1,height*.86);
                c.lineTo(width*.1,height*.14); c.lineTo(width*.85,height*.14);
                c.lineTo(width*.85,height*.39); c.stroke();
                c.beginPath(); c.arc(width*.31,height*.34,width*.065,0,Math.PI*2); c.stroke();
                c.beginPath(); c.moveTo(width*.16,height*.72); c.lineTo(width*.38,height*.5);
                c.lineTo(width*.51,height*.63); c.stroke();
                c.beginPath(); c.moveTo(width*.49,height*.91); c.lineTo(width*.54,height*.7);
                c.lineTo(width*.84,height*.4); c.lineTo(width*.97,height*.53);
                c.lineTo(width*.67,height*.83); c.closePath(); c.stroke();
                c.beginPath(); c.moveTo(width*.78,height*.46); c.lineTo(width*.91,height*.59); c.stroke();
            } else if (kind === "crop") {
                c.beginPath(); c.moveTo(width*.28,height*.05); c.lineTo(width*.28,height*.75); c.lineTo(width*.95,height*.75);
                c.moveTo(width*.05,height*.28); c.lineTo(width*.75,height*.28); c.lineTo(width*.75,height*.95); c.stroke();
            } else if (kind === "rotate") {
                c.beginPath(); c.arc(width*.5,height*.55,width*.32,0,Math.PI*1.7); c.stroke();
                c.beginPath(); c.moveTo(width*.65,height*.08); c.lineTo(width*.78,height*.3); c.lineTo(width*.52,height*.3); c.stroke();
            } else if (kind === "gallery") {
                c.strokeRect(width * .2, height * .15, width * .68, height * .65);
                c.beginPath();
                c.moveTo(width * .12, height * .32);
                c.lineTo(width * .12, height * .9);
                c.lineTo(width * .72, height * .9);
                c.stroke();
                c.beginPath();
                c.moveTo(width * .24, height * .72);
                c.lineTo(width * .43, height * .5);
                c.lineTo(width * .55, height * .63);
                c.lineTo(width * .68, height * .43);
                c.lineTo(width * .84, height * .65);
                c.stroke();
                c.beginPath();
                c.arc(width * .4, height * .34, width * .045, 0, Math.PI * 2);
                c.stroke();
            } else {
                c.beginPath();
                if (kind === "left") {
                    c.moveTo(width * 0.65, height * 0.2);
                    c.lineTo(width * 0.35, height * 0.5);
                    c.lineTo(width * 0.65, height * 0.8);
                } else if (kind === "right") {
                    c.moveTo(width * 0.35, height * 0.2);
                    c.lineTo(width * 0.65, height * 0.5);
                    c.lineTo(width * 0.35, height * 0.8);
                } else {
                    c.moveTo(width * 0.2, height * 0.35);
                    c.lineTo(width * 0.5, height * 0.65);
                    c.lineTo(width * 0.8, height * 0.35);
                }
                c.stroke();
            }
        }
    }
    component UiSpinner: Canvas {
        id: spinner
        property bool running: true
        property color ink: "#ffffff"
        property color track: win.brandTrack
        implicitWidth: 40
        implicitHeight: 40
        antialiasing: true
        Accessible.role: Accessible.ProgressBar
        Accessible.name: "A processar"
        onInkChanged: requestPaint()
        onTrackChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const c = getContext("2d");
            c.reset();
            const radius = Math.min(width, height) / 2 - 4;
            c.lineWidth = 5;
            c.lineCap = "round";
            c.strokeStyle = track;
            c.beginPath();
            c.arc(width / 2, height / 2, radius, 0, 2 * Math.PI);
            c.stroke();
            c.strokeStyle = ink;
            c.beginPath();
            c.arc(width / 2, height / 2, radius, -Math.PI / 2, Math.PI * 0.7);
            c.stroke();
        }
        RotationAnimator on rotation {
            from: 0
            to: 360
            duration: 850
            loops: Animation.Infinite
            running: spinner.running && spinner.visible
        }
    }
    // Desktop page bars live in the outer gutter, outside clipped page content.
    // Qt's attached scrollbar retains native mouse dragging and ListView origins.
    component UiPageScrollBar: Base.ScrollBar {
        id: desktopBar
        property var pageView
        objectName: pageView ? pageView.objectName + "Bar" : ""
        hoverEnabled: true
        parent: win.contentItem
        anchors.right: parent.right
        anchors.rightMargin: 2
        anchors.top: parent.top
        anchors.topMargin: win.pageInset
        anchors.bottom: appFooter.top
        anchors.bottomMargin: win.pageGap
        z: 50
        width: 12
        padding: 2
        orientation: Qt.Vertical
        policy: win.autoHideChrome ? ScrollBar.AlwaysOff : ScrollBar.AsNeeded
        visible: !win.autoHideChrome && win.activePageScroll === pageView && size < 1
        active: visible
        interactive: true
        minimumSize: Math.min(1, 36 / Math.max(1, height))
        contentItem: Rectangle {
            implicitWidth: 8
            radius: 4
            color: desktopBar.pressed ? win.brandPrimary : desktopBar.hovered ? win.ink : win.mutedInk
        }
        background: Rectangle {
            radius: 6
            color: win.softSurface
        }
    }
    component UiLabel: Base.Label {
        // UI values are literal text, including names and notes supplied by users.
        textFormat: Text.PlainText
        color: enabled ? win.ink : win.mutedInk
    }
    component UiButton: Base.Button {
        id: control
        property bool primary: false
        property bool destructive: false
        property color selectedColor: win.brandPrimary
        property string symbol: ""
        property bool wrapText: false
        implicitHeight: win.touchHeight
        padding: 12
        hoverEnabled: true
        palette: lightPalette
        Accessible.name: text
        contentItem: Item {
            implicitWidth: control.symbol ? 20 : buttonText.implicitWidth
            implicitHeight: control.symbol ? 20 : control.wrapText ? buttonText.contentHeight : buttonText.implicitHeight
            Text {
                id: buttonText
                anchors.centerIn: parent
                width: parent.width
                wrapMode: control.wrapText ? Text.Wrap : Text.NoWrap
                elide: control.wrapText ? Text.ElideNone : Text.ElideRight
                visible: !control.symbol
                text: control.text
                font: control.font
                color: control.enabled && (control.primary || control.checked) ? "#ffffff" : control.destructive && control.enabled ? win.danger : win.ink
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            UiIcon {
                anchors.centerIn: parent
                width: 20
                height: 20
                visible: !!control.symbol
                kind: control.symbol
                ink: control.enabled && (control.primary || control.checked) ? "#ffffff" : control.destructive && control.enabled ? win.danger : win.ink
            }
        }
        background: Rectangle {
            radius: 8
            color: !control.enabled ? win.disabledSurface : control.primary || control.checked ? (control.down ? Qt.darker(control.selectedColor, 1.25) : control.hovered ? Qt.darker(control.selectedColor, 1.1) : control.selectedColor) : control.down ? win.pressedSurface : control.hovered ? win.hoverSurface : control.destructive ? "#fff1f0" : "#ffffff"
            border.color: control.visualFocus ? win.brandPrimary : control.destructive && control.enabled ? win.danger : win.outline
            border.width: control.visualFocus ? 3 : 1
            // White inset separates the focus ring from a filled primary button.
            Rectangle {
                anchors.fill: parent
                anchors.margins: 3
                radius: 3
                color: "transparent"
                border.width: control.visualFocus ? 1 : 0
                border.color: control.primary || control.checked ? "#ffffff" : win.brandPrimary
            }
        }
    }
    component UiIconButton: UiButton {
        implicitWidth: win.touchHeight
        implicitHeight: win.touchHeight
        ToolTip.visible: hovered && text.length > 0
        ToolTip.text: text
    }
    // Keep 48 px hit targets while making the header surfaces visually smaller.
    component UiHeaderButton: UiIconButton {
        id: headerControl
        property color surface: "#ffffff"
        property color edge: win.outline
        background: Item {
            Rectangle {
                anchors.centerIn: parent
                width: win.compact ? 36 : parent.width
                height: win.compact ? 36 : parent.height
                radius: 8
                color: headerControl.down ? win.pressedSurface : headerControl.surface
                border.color: headerControl.visualFocus ? "#ffd66a" : headerControl.edge
                border.width: headerControl.visualFocus ? 3 : 1
            }
        }
    }
    component UiTextField: Base.TextField {
        id: control
        implicitHeight: win.touchHeight
        padding: 12
        palette: lightPalette
        color: win.ink
        placeholderTextColor: win.mutedInk
        selectionColor: win.brandPrimary
        selectedTextColor: "#ffffff"
        Accessible.name: placeholderText
        background: Rectangle {
            radius: 8
            color: control.enabled ? "#ffffff" : win.disabledSurface
            border.color: control.activeFocus ? win.brandPrimary : win.outline
            border.width: control.activeFocus ? 2 : 1
        }
    }
    component UiTextArea: Base.TextArea {
        id: control
        implicitHeight: Math.max(88, contentHeight + topPadding + bottomPadding)
        padding: 12
        palette: lightPalette
        color: win.ink
        placeholderTextColor: win.mutedInk
        selectionColor: win.brandPrimary
        selectedTextColor: "#ffffff"
        Accessible.name: placeholderText
        background: Rectangle {
            radius: 8
            color: control.enabled ? "#ffffff" : win.disabledSurface
            border.color: control.activeFocus ? win.brandPrimary : win.outline
            border.width: control.activeFocus ? 2 : 1
        }
    }
    component UiComboBox: Base.ComboBox {
        id: control
        implicitHeight: win.touchHeight
        leftPadding: 12
        rightPadding: 36
        palette: lightPalette
        contentItem: Text {
            text: control.displayText
            font: control.font
            color: win.ink
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: UiIcon {
            width: 18
            height: 18
            x: control.width - width - 12
            y: (control.height - height) / 2
            kind: "down"
        }
        background: Rectangle {
            radius: 8
            color: control.enabled ? "#ffffff" : win.disabledSurface
            border.color: control.visualFocus ? win.brandPrimary : win.outline
            border.width: control.visualFocus ? 2 : 1
        }
        delegate: Base.ItemDelegate {
            required property int index
            required property var modelData
            width: control.width
            implicitHeight: win.touchHeight
            text: control.textRole ? modelData[control.textRole] : modelData
            highlighted: control.highlightedIndex === index
            contentItem: Text {
                text: parent.text
                font: control.font
                color: parent.highlighted ? "#ffffff" : win.ink
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                color: parent.highlighted ? win.brandPrimary : "#ffffff"
            }
        }
        popup: Base.Popup {
            y: control.height + 4
            width: control.width
            padding: 2
            implicitHeight: Math.min(contentItem.implicitHeight + 4, win.height * 0.5)
            palette: lightPalette
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: control.popup.visible ? control.delegateModel : null
                currentIndex: control.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator {}
            }
            background: Rectangle {
                color: "#ffffff"
                radius: 8
                border.color: win.outline
            }
        }
    }

    property string page: win.reportData.id ? "report" : "catalog"
    // Keep the catalogue model stable while editing a report. Restore by ID rather
    // than absolute pixels: adding a draft can change the height of a card.
    property var catalogRows: []
    // Serialized form of catalogRows, kept to detect "no change" without
    // serializing the displayed catalogue again on every refresh.
    property string catalogEncoded: "[]"
    property string catalogScope: ""
    property var catalogAnchor: ({})
    property bool openingCatalogReport: false
    function rememberCatalogPosition() {
        const y = chargerList.contentY;
        let index = chargerList.indexAt(1, y + 1);
        if (index < 0)
            index = chargerList.indexAt(1, y + 16);
        const item = chargerList.itemAtIndex(index);
        catalogAnchor = {
            id: index >= 0 ? catalogRows[index].id : "",
            offset: item ? y - item.y : 0,
            y: y,
            scope: lem.contractId() + "|" + search.text
        };
    }
    function addressLine(details) {
        const base = String(details.address || "").trim();
        const postal = String(details.postal_code || "").trim();
        // Some imports already include the postal code in the address.
        return postal && !base.includes(postal) ? [base, postal].filter(v => !!v).join(", ") : base;
    }
    function coordinate(value, limit) {
        if (value === null || value === undefined || typeof value === "boolean")
            return null;
        // Numeric JSON may stringify in exponential notation near zero. Validate
        // the number itself; keep strict syntax validation for imported strings.
        if (typeof value === "number")
            return isFinite(value) && Math.abs(value) <= limit ? value : null;
        const text = String(value).trim().replace(",", ".");
        // Avoid parseFloat accepting partial values or empty cells becoming zero.
        if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/.test(text))
            return null;
        const number = Number(text);
        return isFinite(number) && Math.abs(number) <= limit ? number : null;
    }
    function coordinateText(value) {
        // Valid coordinates are bounded to +/-180. Expand the small-number
        // exponent form to plain decimal for Maps, without rounding the value.
        const text = String(value);
        const tiny = /^(-?)(\d)(?:\.(\d+))?e-(\d+)$/.exec(text);
        return tiny ? tiny[1] + "0." + "0".repeat(Number(tiny[4]) - 1) + tiny[2] + (tiny[3] || "") : text;
    }
    function navigationUrl(details) {
        const lat = coordinate(details.latitude, 90);
        const lon = coordinate(details.longitude, 180);
        const hasAddress = String(details.address || "").trim().length > 0;
        const destination = lat !== null && lon !== null ? coordinateText(lat) + "," + coordinateText(lon) : hasAddress ? addressLine(details) : "";
        // A fixed HTTPS host and encoded destination keep imported text out of URL parameters.
        return destination ? "https://www.google.com/maps/dir/?api=1&travelmode=driving&destination=" + encodeURIComponent(destination) : "";
    }
    // Search the charger's values only. Matching the serialized JSON also matched
    // field names ("id", "status") and report/charger UUIDs, returning every row.
    function chargerMatches(charger, needle) {
        if (!needle)
            return true;
        const parts = [String(charger.external_id || "")];
        const details = charger.details || {};
        for (const key in details) {
            if (key === "source" || obsoletePcveField(key)) continue;
            const value = details[key];
            if (value !== null && typeof value === "object") {
                for (const column in value)
                    parts.push(String(value[column]));
            } else if (value !== null && value !== undefined)
                parts.push(String(value));
        }
        // Search new terminology even in a catalogue cached by an older app.
        return lem.connectorText(parts.join("\n")).toLowerCase().includes(lem.connectorText(needle).toLowerCase());
    }
    function refreshCatalog(restore) {
        if (page !== "catalog" || openingCatalogReport)
            return;
        const scope = lem.contractId() + "|" + search.text;
        const sameScope = catalogScope === scope;
        if (!restore && sameScope)
            rememberCatalogPosition();
        const needle = search.text.trim().toLowerCase();
        const rows = lem.chargers.filter(c => chargerMatches(c, needle));
        const encodedRows = JSON.stringify(rows);
        if (encodedRows === catalogEncoded && !restore && sameScope)
            return;
        // Keep a detached snapshot, not live QVariant-backed wrappers: changes
        // to report summaries must notify the ListView and completion colors.
        catalogRows = JSON.parse(encodedRows);
        catalogEncoded = encodedRows;
        catalogScope = scope;
        const anchor = catalogAnchor.scope === scope ? catalogAnchor : {};
        Qt.callLater(function () {
            if (win.page !== "catalog")
                return;
            chargerList.forceLayout();
            const index = win.catalogRows.findIndex(c => c.id === anchor.id);
            if (index >= 0) {
                chargerList.positionViewAtIndex(index, ListView.Beginning);
                const item = chargerList.itemAtIndex(index);
                if (item)
                    chargerList.contentY = item.y + anchor.offset;
            } else
                chargerList.contentY = anchor.y || (chargerList.originY - (chargerList.headerItem ? chargerList.headerItem.height : 0));
            chargerList.returnToBounds();
        });
    }
    property var stationReportOrigin: null
    property var pendingReportOpen: null
    function openStationReport(contractId, chargerId, reportId) {
        const stationId = pcveDetailsDialog.stationId;
        const index = pcveList.model.findIndex(row => row.id === stationId);
        const card = pcveList.itemAtIndex(index);
        const origin = {
            stationId: stationId, userId: pcvePage.userId,
            cardOffset: card ? card.y - pcveList.contentY : 0,
            detailsY: pcveDetailsScroll.contentItem.contentY,
            chrome: chromeReveal
        };
        if (lem.contractId() !== contractId)
            lem.selectContract(contractId);
        if (lem.contractId() === contractId)
            openCatalogReport(chargerId, reportId, origin);
    }
    function leaveReport() {
        const origin = stationReportOrigin;
        if (page !== "report" || !origin || origin.userId !== pcvePage.userId) {
            page = "catalog";
            return;
        }
        page = "pcves";
        Qt.callLater(function () {
            if (win.page !== "pcves" || origin.userId !== pcvePage.userId)
                return;
            pcveList.forceLayout();
            const index = pcveList.model.findIndex(row => row.id === origin.stationId);
            if (index < 0)
                return;
            pcveList.positionViewAtIndex(index, ListView.Beginning);
            const card = pcveList.itemAtIndex(index);
            if (card)
                pcveList.contentY = card.y - origin.cardOffset;
            pcveList.returnToBounds();
            win.chromeReveal = origin.chrome;
            pcveDetailsDialog.stationId = origin.stationId;
            pcveDetailsDialog.restoreY = origin.detailsY;
            pcveDetailsDialog.open();
        });
    }
    function openCatalogReport(chargerId, reportId, origin) {
        if (!origin)
            rememberCatalogPosition();
        openingCatalogReport = true;
        const opened = lem.openReport(chargerId, reportId);
        if (opened !== false) {
            if (reportId && lem.current.id !== reportId) {
                pendingReportOpen = {id: reportId, origin: origin || null, contractId: lem.contractId()};
            } else {
                stationReportOrigin = origin || null;
                page = "report";
            }
        }
        openingCatalogReport = false;
        if (!reportId && opened === false) {
            win.showFeedback = false;
            draftSaveErrorDialog.detail = lem.message;
            draftSaveErrorDialog.open();
        }
    }
    onPageChanged: {
        pendingReportOpen = null;
        if (page === "catalog")
            Qt.callLater(function () {
                refreshCatalog(true);
            });
        if (page !== "pcves") {
            pcveDetailsDialog.close();
            pcveEditDialog.close();
            pcveImportDialog.close();
        }
        if (page !== "admin") {
            contractIdsDialog.close();
            detachPcveDialog.close();
            if (pcveImportDialog.contractId)
                pcveImportDialog.close();
        }
        resetScrollChrome();
        if (page !== "report") {
            stationReportOrigin = null;
            lem.reportSyncBoundary();
        }
    }
    property int selectedSection: 0
    property string sectionReportId: win.reportData.id || ""
    property var sectionPositions: ({})
    property string focusedCheckpoint: ""
    onSectionReportIdChanged: {
        resetScrollChrome();
        sectionPositions = {};
        selectedSection = 0;
        focusedCheckpoint = "";
        Qt.callLater(function () {
            reportScroll.contentY = 0;
        });
    }
    // Save/restore each group's scroll offset and release deferred photograph synchronization.
    function selectSection(index) {
        if (index === selectedSection || index < 0 || index >= lem.sections.length)
            return;
        const positions = Object.assign({}, sectionPositions);
        positions[selectedSection] = reportScroll.contentY;
        sectionPositions = positions;
        selectedSection = index;
        resetScrollChrome();
        lem.reportSyncBoundary();
    }
    onSelectedSectionChanged: Qt.callLater(function () {
        reportScroll.contentY = Math.max(0, Math.min(sectionPositions[selectedSection] || 0, reportScroll.contentHeight - reportScroll.height));
    })
    function itemIssues(item) {
        const a = answerFor(item.id), value = String(a.value || "").trim(), issues = [];
        if (item.kind === "status" && String(a.value || "") && ["OK", "NOK", "NA"].indexOf(String(a.value)) < 0)
            issues.push("Escolha OK, NOK ou N/A");
        if (!value && !item.optional)
            issues.push("Falta resposta");
        if (value && Measurements.digits(item.id) && Measurements.format(item.id, value) === null)
            issues.push("Valor inválido: até " + Measurements.digits(item.id) + " algarismos inteiros e 2 decimais, ou Não Aplicável");
        if (item.reading_required && /^(OK|NOK)$/i.test(value))
            issues.push("Falta valor medido");
        if (!item.notes_disabled && (value === "NOK" || a.assessment === "NOK") && !String(a.notes || "").trim())
            issues.push("Falta observação NOK");
        if (item.na_notes_required && Measurements.notApplicable(value) && !String(a.notes || "").trim())
            issues.push("Falta observação N/A");
        if (item.signature_required) {
            const signature = (((lem.current.payload || {}).signatures || {})[item.id]);
            if (!signature)
                issues.push("Falta assinatura do técnico");
            else if (signature.signed_by !== value || !(lem.current.photos || []).some(p => p.id === signature.photo_id && p.checkpoint === item.id && p.mime === "image/png"))
                issues.push("Assinatura inválida; assine novamente");
        }
        if (item.photo_required && (item.photo_always || !Measurements.notApplicable(value)) && !photographsFor(item.id).length)
            issues.push("Falta fotografia");
        return issues;
    }
    // Local review guidance only: the C++ finish action and server independently validate closure.
    function pendingItems() {
        const result = [];
        lem.sections.forEach(function (section, index) {
            section.items.forEach(function (item) {
                const issues = itemIssues(item);
                if (issues.length)
                    result.push({
                        section: index,
                        id: item.id,
                        label: item.label,
                        issues: issues.join(" • ")
                    });
            });
        });
        return result;
    }
    function groupMissing(section) {
        return section.items.filter(function (item) {
            return itemIssues(item).length > 0;
        }).length;
    }
    // Shared bindings avoid traversing the checklist for each progress label.
    readonly property int answeredItemCount: lem.sections.reduce(function (n, s) {
            return n + sectionAnswered(s);
        }, 0)
    readonly property int checklistItemCount: lem.sections.reduce(function (n, s) {
            return n + s.items.length;
        }, 0)
    function totalAnswered() { return answeredItemCount; }
    function totalItems() { return checklistItemCount; }
    function jumpTo(section, id) {
        Qt.inputMethod.hide();
        reportScroll.cancelFlick();
        selectSection(section);
        // Review navigation must reveal the groups even when the target is in
        // the current section (selectSection deliberately does nothing there).
        resetScrollChrome();
        focusedCheckpoint = id;
        reviewDialog.close();
        groupDialog.close();
        const reportId = win.sectionReportId;
        Qt.callLater(function () {
            if (win.page !== "report" || win.sectionReportId !== reportId || win.selectedSection !== section || win.focusedCheckpoint !== id)
                return;
            const panel = sectionPanels.itemAt(section);
            if (!panel || !panel.checkpoints)
                return;
            const card = panel.checkpoints.itemAt(panel.modelData.items.findIndex(function (i) {
                return i.id === id;
            }));
            if (card) {
                card.forceActiveFocus(Qt.OtherFocusReason);
                // Lazy group layout, saved-position restoration and committing
                // the previous editor must settle before measuring the card.
                Qt.callLater(function () {
                    if (win.page !== "report" || win.sectionReportId !== reportId || win.selectedSection !== section || win.focusedCheckpoint !== id)
                        return;
                    const y = card.mapToItem(reportScroll.contentItem, 0, 0).y;
                    const inset = win.pageTopInset;
                    const origin = reportScroll.originY;
                    const maximum = origin + Math.max(0, reportScroll.contentHeight - reportScroll.height);
                    // Overlay groups cover the top of the viewport. Subtract
                    // their full height, and clamp at the real list end rather
                    // than adding blank space beneath the final questions.
                    reportScroll.contentY = Math.max(origin, Math.min(y - inset, maximum));
                    win.lastPageScrollY = win.pageScrollOffset();
                });
            }
        });
    }
    function transferSize(bytes) {
        if (bytes < 0)
            return "tamanho desconhecido";
        if (bytes < 1024)
            return Math.round(bytes) + " B";
        if (bytes < 1048576)
            return (bytes / 1024).toFixed(1) + " KiB";
        return (bytes / 1048576).toFixed(1) + " MiB";
    }
    function openPhotoDownloads() {
        photoDownloadDialog.summary = lem.photoDownloadSummary();
        photoDownloadDialog.reportId = lem.current.id;
        photoDownloadDialog.open();
    }
    function openReview() {
        Qt.inputMethod.hide();
        reviewDialog.open();
    }
    function sectionAnswered(section) {
        const answers = (win.reportData.payload || {}).answers || {};
        return section.items.filter(function (item) {
            const answer = answers[item.id];
            return item.optional || (answer && String(answer.value || "").trim().length > 0);
        }).length;
    }
    function navigateBack() {
        if (Qt.inputMethod.visible)
            Qt.inputMethod.hide();
        else
            win.leaveReport();
    }
    Shortcut {
        id: reportBackShortcut
        objectName: "reportBackShortcut"
        // Android's Back sequence must not also bind a keyboard's Backspace.
        sequences: win.androidCamera ? ["Back"] : [StandardKey.Back]
        enabled: !win.splashActive && (win.page === "report" || win.page === "pcves")
                 && !reviewDialog.opened && !groupDialog.opened && !detailsDialog.opened && !pcveDetailsDialog.opened
                 && !pcveEditDialog.opened && !pcveImportDialog.opened
                 && !photoPreview.opened && !cropDialog.opened && !removePhotoDialog.opened && !deleteReportDialog.opened
                 && !removeNotesDialog.opened && !photoDownloadDialog.opened && !statusDialog.opened && !loginDialog.opened
                 && !clearLocalCopiesDialog.opened && !reportMenu.opened && !navMenu.opened
        onActivated: win.navigateBack()
    }
    readonly property var pcveRecords: lem.stations || []
    readonly property bool canManagePcves: !!lem.desktopStationManagement && lem.authenticated && lem.user.role === "admin"
    readonly property var importPreviewList: contractExcelPanel.previewList
    property string photoItem: ""
    property string exportKind: "pdf"
    property bool canDeleteReport: !!win.reportData.id && (lem.user.role === "admin" || (win.reportData.status === "draft" && win.reportData.owner_id === lem.user.id))
    // Presentation guard for protected/closed/other-owned reports. Server checks are authoritative.
    readonly property bool correctingReport: !!win.reportData.correction && win.reportData.correction.state === "editing"
    readonly property bool correctionPermitted: !correctingReport || (!!lem.desktopCorrections && lem.user.role === "admin" && win.reportData.correction.editor_id === lem.user.id)
    property bool editing: !(win.reportData.template || {}).frozen_upload && correctionPermitted && !win.reportData._conflict && win.reportData.id !== undefined && win.reportData.status !== "closed" && win.reportData.owner_id === lem.user.id
    property var fields: [
        {
            key: "external_id",
            label: "ID do PCVE"
        },
        {
            key: "address",
            label: "Morada"
        },
        {
            key: "postal_code",
            label: "Código postal"
        },
        {
            key: "latitude",
            label: "Latitude"
        },
        {
            key: "longitude",
            label: "Longitude"
        },
        {
            key: "region",
            label: "CCDR"
        },
        {
            key: "cpe",
            label: "CPE associado"
        },
        {
            key: "energy_supply",
            label: "Fornecimento de energia"
        },
        {
            key: "serial",
            label: "Número(s) de série"
        },
        {
            key: "manufacturer",
            label: "Fabricante"
        },
        {
            key: "model",
            label: "Modelo"
        },
        {
            key: "power",
            label: "Potência"
        },
        {
            key: "power_limit",
            label: "Limitação de potência (kVA)"
        },
        {
            key: "connectors",
            label: "Tomadas / fichas"
        }
    ]
    function obsoletePcveField(key) {
        const field = key.replace(/^extra\./, "").replace(/_/g, " ").trim().toLowerCase();
        if (key.startsWith("extra.") && ["id", "external id", "station id", "id do pcve", "id de pcve", "identificador"].includes(field))
            return true;
        return ["location", "localização", "localizacao", "nome do local", "local", "site name",
                "localização / nome do local", "localizacao / nome do local", "site id", "id do site"].includes(field);
    }
    function previewDetails(record) {
        const details = record.details || {};
        const source = details.source || {};
        if (Object.keys(source).length)
            return Object.keys(source).filter(k => k !== "Station ID").map(k => ({label: k, value: lem.connectorText(String(source[k] === null ? "" : source[k]))}));
        return Object.keys(details).filter(k => k !== "source").map(k => ({label: (fields.find(f => f.key === k) || {}).label || k, value: lem.connectorText(String(details[k] === null ? "" : details[k]))}));
    }
    function pcveFields(details) {
        const rows = [];
        for (const field of fields) {
            const value = details[field.key];
            if (field.key === "external_id" || value === undefined || value === null || String(value).trim() === "")
                continue;
            const unit = field.key === "power" && details.power_unit ? " " + details.power_unit : "";
            rows.push({label: field.label, value: lem.connectorText(String(value)) + unit});
        }
        for (const key of Object.keys(details)) {
            const value = details[key];
            if (key.startsWith("extra.") && !obsoletePcveField(key) && value != null && String(value).trim() !== "")
                rows.push({label: key.slice(6), value: String(value)});
        }
        return rows;
    }
    function pcveEditFields(details) {
        return fields.filter(field => field.key !== "external_id").concat(
            [{key: "power_unit", label: "Unidade de potência"}],
            Object.keys(details).filter(key => key.startsWith("extra.") && !obsoletePcveField(key)).map(key => ({key: key, label: key.slice(6)})));
    }
    function previewPcveSheet(sheet) {
        const recommended = sheet.recommended_mapping;
        if (recommended) {
            pcveImportDialog.previewSheet(sheet.name, recommended.header_row, recommended.columns);
            return;
        }
        const headers = (sheet.sample || [])[0] || [];
        const columns = {};
        const aliases = {
            "id": "external_id", "identificador": "external_id", "id de pcve": "external_id",
            "id do carregador": "external_id", "série": "serial", "nº(s) série": "serial",
            "station id": "external_id", "id do pcve": "external_id",
            "cpe": "cpe", "fornecimento": "energy_supply", "tomadas": "connectors"
        };
        for (const raw of headers) {
            const header = raw.trim();
            if (!header || obsoletePcveField(header))
                continue;
            const lower = header.toLowerCase();
            const field = fields.find(entry => entry.key === lower || entry.label.toLowerCase() === lower);
            const key = Object.prototype.hasOwnProperty.call(aliases, lower) ? aliases[lower] : (field || {}).key;
            if (key === "external_id" && columns.external_id) continue;
            columns[key && !Object.prototype.hasOwnProperty.call(columns, key) ? key : "extra." + header] = header;
        }
        if (!columns.external_id) {
            pcveImportDialog.importNotice = "Use ID ou Identificador na primeira linha, ou o formato Stations.";
            return;
        }
        pcveImportDialog.previewSheet(sheet.name, 1, columns);
    }
    // Resolve answers from the active report projection; do not persist delegate-local copies.
    function answerFor(id) {
        const r = win.reportData;
        return r.payload && r.payload.answers && r.payload.answers[id] ? r.payload.answers[id] : {
            value: "",
            notes: ""
        };
    }
    function notesForDisplay(id) {
        const answer = answerFor(id);
        if (id === "EST.15" && answer.notes === "Sem POS" && answer.suggested_notes === answer.notes)
            return "Sem TPM";
        return answer.notes || "";
    }
    function photosFor(id) {
        return win.reportPhotos[id] || [];
    }
    function photographsFor(id) { return photosFor(id).filter(p => !(p.mime || "").startsWith("video/")); }
    header: ToolBar {
        objectName: "appHeader"
        padding: win.compact ? 0 : 4
        background: Rectangle {
            color: "#ffffff"
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 2
                color: win.brandAccent
            }
        }
        contentItem: ColumnLayout {
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                spacing: win.compact ? 2 : 10
                UiHeaderButton {
                    objectName: "reportBackButton"
                    visible: win.page === "report" && (win.compact || !!win.stationReportOrigin)
                    symbol: "left"
                    implicitWidth: win.touchHeight
                    Accessible.name: win.stationReportOrigin ? "Voltar ao PCVE" : "Voltar aos contratos"
                    onClicked: {
                        Qt.inputMethod.hide();
                        win.leaveReport();
                    }
                }
                Image {
                    objectName: "headerLogo"
                    source: win.logoSource
                    // The SVG is tightly cropped. Leave white space above and below it,
                    // including the two pixels occupied by the toolbar's bottom rule.
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 34
                    Layout.leftMargin: win.compact ? 6 : 10
                    Layout.topMargin: 4
                    Layout.bottomMargin: 6
                    fillMode: Image.PreserveAspectFit
                    Accessible.name: "Logótipo Lem"
                }
                UiLabel {
                    text: "Lem"
                    color: win.ink
                    font.bold: true
                    font.pixelSize: win.compact ? 18 : 19
                    Layout.leftMargin: win.compact ? 4 : 12
                }
                Item {
                    Layout.fillWidth: true
                }
                UiButton {
                    implicitHeight: win.touchHeight
                    visible: win.width > 640
                    text: "Contratos"
                    checked: win.page === "catalog"
                    enabled: !lem.busy
                    onClicked: win.page = "catalog"
                }
                UiButton {
                    objectName: "pcveCatalogueButton"
                    text: "PCVEs"
                    visible: win.width > 640 && !!lem.user.id
                    checked: win.page === "pcves"
                    enabled: !lem.busy
                    onClicked: win.page = "pcves"
                }
                UiButton {
                    implicitHeight: win.touchHeight
                    text: "Gestão"
                    checked: win.page === "admin"
                    visible: win.width > 640 && lem.user.role === "admin"
                    enabled: !lem.busy
                    onClicked: {
                        win.page = "admin";
                        lem.loadUsers();
                    }
                }
                UiHeaderButton {
                    implicitHeight: win.touchHeight
                    implicitWidth: win.touchHeight
                    id: navigationButton
                    Accessible.name: "Abrir navegação"
                    objectName: "navigationButton"
                    symbol: "menu"
                    visible: win.width <= 640 && win.page !== "report"
                    onClicked: navMenu.visible = !navMenu.visible
                    Base.Menu {
                        id: navMenu
                        popupType: Popup.Item
                        width: 220
                        x: parent.width - width
                        y: parent.height
                        palette: lightPalette
                        background: Rectangle {
                            color: "#ffffff"
                            border.color: win.outline
                            radius: 8
                        }
                        objectName: "navigationMenu"
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                        Base.MenuItem {
                            implicitHeight: win.touchHeight
                            palette: lightPalette
                            text: "Contratos"
                            onTriggered: win.page = "catalog"
                        }
                        Base.MenuItem {
                            objectName: "pcveCatalogueMenuItem"
                            implicitHeight: visible ? win.touchHeight : 0
                            palette: lightPalette
                            text: "PCVEs"
                            visible: !!lem.user.id
                            enabled: !lem.busy
                            onTriggered: win.page = "pcves"
                        }
                        Base.MenuItem {
                            implicitHeight: visible ? win.touchHeight : 0
                            palette: lightPalette
                            text: "Gestão"
                            visible: lem.user.role === "admin"
                            onTriggered: {
                                win.page = "admin";
                                lem.loadUsers();
                            }
                        }
                    }
                }
                UiHeaderButton {
                    visible: win.page === "report"
                    symbol: "menu"
                    implicitWidth: win.touchHeight
                    Accessible.name: "Opções do report"
                    objectName: "reportOptionsButton"
                    onClicked: reportMenu.visible = !reportMenu.visible
                    Base.Menu {
                        id: reportMenu
                        objectName: "reportMenu"
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                        popupType: Popup.Item
                        width: Math.min(280, win.width - 24)
                        x: parent.width - width
                        y: parent.height
                        palette: lightPalette
                        background: Rectangle {
                            color: "white"
                            radius: 8
                            border.color: win.outline
                        }
                        Base.MenuItem {
                            implicitHeight: win.touchHeight
                            text: "Atualizar report do servidor"
                            enabled: !win.reportNetworkActivity
                            onTriggered: lem.refreshReport()
                        }
                        Base.MenuItem {
                            objectName: "downloadReportPhotosAction"
                            implicitHeight: win.touchHeight
                            text: lem.downloadingPhotos ? "Descarregamento em curso…" : "Descarregar todas as evidências"
                            enabled: !!win.reportData.id && !lem.downloadingPhotos
                            onTriggered: win.openPhotoDownloads()
                        }
                        Base.MenuItem {
                            implicitHeight: win.touchHeight
                            text: "Cancelar fotografias restantes"
                            visible: lem.downloadingPhotos
                            height: visible ? implicitHeight : 0
                            onTriggered: lem.cancelReportPhotoDownloads()
                        }
                        Base.MenuItem {
                            implicitHeight: win.touchHeight
                            text: "Permitir limpeza destas fotografias"
                            visible: !!win.reportData.offline_photos
                            height: visible ? implicitHeight : 0
                            enabled: !lem.currentReportDownloadingPhotos
                            onTriggered: lem.releaseReportPhotos()
                        }
                        Base.MenuItem {
                            objectName: "correctClosedReportAction"
                            text: "Corrigir report fechado…"
                            visible: !!lem.desktopCorrections && lem.user.role === "admin" && win.reportData.status === "closed" && !win.correctingReport
                            height: visible ? implicitHeight : 0
                            enabled: !win.reportNetworkActivity && lem.authenticated && !win.reportData.dirty && win.reportData.revision > 0
                            onTriggered: beginCorrectionDialog.open()
                        }
                        Base.MenuItem {
                            objectName: "correctionDatesAction"
                            text: "Editar início e fim…"
                            visible: win.correctingReport && win.editing
                            height: visible ? implicitHeight : 0
                            enabled: !lem.busy
                            onTriggered: {
                                correctionStart.text = (win.reportData.payload || {}).started || win.reportData.started || "";
                                correctionEnd.text = (win.reportData.payload || {}).finished || win.reportData.finished || "";
                                correctionDatesDialog.open();
                            }
                        }
                        Base.MenuItem {
                            objectName: "transferReportAction"
                            implicitHeight: win.touchHeight
                            text: "Transferir propriedade"
                            visible: lem.user.role === "admin" && win.reportData.status === "draft" && !win.correctingReport
                            height: visible ? implicitHeight : 0
                            enabled: !win.reportNetworkActivity && win.reportData.revision > 0 && !win.reportData.dirty && !win.reportData._conflict
                            onTriggered: {
                                transferDialog.reportId = lem.current.id;
                                lem.loadReportOwners();
                                transferDialog.open();
                            }
                        }
                        Base.MenuItem {
                            implicitHeight: win.touchHeight
                            text: "Criar rascunho como cópia"
                            enabled: !!win.reportData.id && !win.reportNetworkActivity
                            onTriggered: copyReportDialog.open()
                        }
                        Base.MenuItem {
                            implicitHeight: win.touchHeight
                            text: "Dados do carregador"
                            onTriggered: detailsDialog.open()
                        }
                        Base.MenuItem {
                            implicitHeight: win.touchHeight
                            text: "Estado da operação"
                            onTriggered: statusDialog.open()
                        }
                        Base.MenuItem {
                            objectName: "deleteReportAction"
                            implicitHeight: win.touchHeight
                            visible: win.canDeleteReport && win.correctionPermitted
                            height: visible ? implicitHeight : 0
                            text: win.correctingReport ? "Cancelar e apagar cópia de correção…" : win.reportData.status === "closed" ? "Apagar report fechado…" : "Apagar rascunho…"
                            enabled: !win.reportNetworkActivity
                            onTriggered: {
                                deleteReportDialog.reportId = lem.current.id;
                                deleteReportDialog.reportIdentifier = lem.current.identifier || "";
                                deleteReportDialog.closedReport = lem.current.status === "closed";
                                deleteReportDialog.open();
                            }
                        }
                        Base.MenuItem {
                            id: exportPdfAction
                            implicitHeight: win.touchHeight
                            objectName: "exportPdfAction"
                            visible: !!win.reportData.id && win.reportData.status === "closed"
                            height: visible ? implicitHeight : 0
                            text: "Exportar PDF"
                            enabled: !!win.reportData.id && win.reportData.status === "closed" && !lem.busy
                            onTriggered: {
                                if (!lem.prepareExport("pdf"))
                                    return;
                                win.exportKind = "pdf";
                                exportDialog.selectedFile = lem.defaultExport("pdf");
                                exportDialog.open();
                            }
                        }
                        Base.MenuItem {
                            id: exportBundleAction
                            implicitHeight: win.touchHeight
                            objectName: "exportBundleAction"
                            visible: !!win.reportData.id && win.reportData.status === "closed"
                            height: visible ? implicitHeight : 0
                            text: "PDF + evidências (ZIP)"
                            enabled: !!win.reportData.id && win.reportData.status === "closed" && !lem.busy
                            onTriggered: {
                                if (!lem.prepareExport("bundle"))
                                    return;
                                win.exportKind = "bundle";
                                exportDialog.selectedFile = lem.defaultExport("bundle");
                                exportDialog.open();
                            }
                        }
                    }
                }
                UiHeaderButton {
                    id: accountControl
                    objectName: "accountButton"
                    symbol: "account"
                    Layout.preferredWidth: win.touchHeight
                    Layout.preferredHeight: win.touchHeight
                    enabled: !lem.busy
                    property bool demoSession: lem.server.indexOf("demo:") === 0
                    text: demoSession ? "Demonstração · sem sessão no servidor"
                          : lem.authenticated ? "Sessão iniciada · " + (lem.user.name || lem.user.email || "Conta")
                          : lem.sessionPending ? "Sessão guardada · por validar" : "Sem sessão iniciada · Entrar"
                    Accessible.name: text + ". Abrir conta"
                    onClicked: win.openAccount()
                    contentItem: Item {
                        implicitWidth: 28
                        implicitHeight: 28
                        UiIcon {
                            kind: "account"
                            anchors.centerIn: parent
                            width: 22
                            height: 22
                            ink: accountControl.demoSession ? "#254f87" : lem.authenticated ? "#104f49" : "#704000"
                        }
                        Rectangle {
                            objectName: "accountBadge"
                            width: 14
                            height: 14
                            radius: 9
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.rightMargin: -3
                            anchors.bottomMargin: -3
                            color: accountControl.demoSession ? "#254f87" : lem.authenticated ? win.brandPrimary : "#8a5200"
                            UiIcon {
                                objectName: "accountStatusIcon"
                                anchors.centerIn: parent
                                width: 10
                                height: 10
                                ink: "#ffffff"
                                kind: accountControl.demoSession ? "demo" : lem.authenticated ? "check" : "warning"
                            }
                        }
                    }
                    surface: demoSession ? "#e5eef9" : lem.authenticated ? "#e2f2ec" : "#fff0cd"
                    edge: demoSession ? "#254f87" : lem.authenticated ? "#4e9987" : "#b37b20"
                }
            }
        }
    }
    SignatureDialog {
        id: signatureDialog
        parent: Overlay.overlay
        onSaveRequested: function(checkpoint, png) {
            if (lem.saveSignature(checkpoint, png)) close();
        }
    }
    Dialog {
        id: qveCopyDialog
        objectName: "qveCopyDialog"
        parent: Overlay.overlay
        property var sources: []
        property string scope: "QVE"
        title: scope === "QVE" ? "Copiar QVE de outro carregador" : "Copiar Medições de outro carregador"
        modal: true
        width: Math.min(620, win.width - 24)
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            sources = scope === "QVE" ? lem.qveCopySources() : lem.measurementCopySources();
            qveSourceCombo.currentIndex = sources.length ? 0 : -1;
            standardButton(Dialog.Ok).enabled = sources.length > 0;
            standardButton(Dialog.Ok).implicitHeight = 48;
            standardButton(Dialog.Cancel).implicitHeight = 48;
        }
        onAccepted: {
            if (qveSourceCombo.currentIndex >= 0) {
                if (scope === "QVE") lem.copyQveFrom(sources[qveSourceCombo.currentIndex].id);
                else lem.copyMeasurementsFrom(sources[qveSourceCombo.currentIndex].id);
            }
        }
        contentItem: ColumnLayout {
            spacing: 12
            UiLabel {
                text: qveCopyDialog.scope === "QVE" ? "Confirme que os carregadores partilham o mesmo QVE. As respostas, observações e evidências deste grupo serão substituídas pelas da origem." : "As respostas, observações e evidências de MAN.04 a MAN.10 serão substituídas pelas da origem. Confirme que se aplicam a este carregador."
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            ComboBox {
                id: qveSourceCombo
                objectName: "qveCopySource"
                implicitHeight: 48
                Layout.fillWidth: true
                model: qveCopyDialog.sources
                textRole: "label"
            }
            UiLabel {
                text: "Não há reports de outros carregadores disponíveis neste contrato."
                visible: !qveCopyDialog.sources.length
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
        }
    }

    Dialog {
        id: removeNotesDialog
        objectName: "removeNotesDialog"
        property string reportId: ""
        property string checkpoint: ""
        modal: true
        title: "Remover observação?"
        anchors.centerIn: parent
        width: Math.min(win.width - 32, 420)
        contentItem: ColumnLayout {
            UiLabel {
                text: "O texto desta observação será apagado. A resposta e as fotografias mantêm-se."
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            UiButton {
                text: "Cancelar"
                objectName: "cancelRemoveNotes"
                Layout.fillWidth: true
                onClicked: removeNotesDialog.close()
            }
            UiButton {
                text: "Remover observação"
                objectName: "confirmRemoveNotes"
                Layout.fillWidth: true
                primary: true
                enabled: !lem.busy
                onClicked: {
                    if (lem.current.id === removeNotesDialog.reportId && win.editing) {
                        Qt.inputMethod.hide();
                        lem.answer(removeNotesDialog.checkpoint, win.answerFor(removeNotesDialog.checkpoint).value || "", "");
                    }
                    removeNotesDialog.close();
                }
            }
        }
    }
    property string lastMessage: lem.message
    property bool showFeedback: false
    onLastMessageChanged: {
        if (lastMessage && feedbackReady) {
            showFeedback = true;
            feedbackTimer.restart();
        }
    }
    property bool feedbackReady: false
    Component.onCompleted: Qt.callLater(function () {
        feedbackReady = true;
        refreshCatalog(true);
    })
    Timer {
        id: feedbackTimer
        interval: 10000
        onTriggered: win.showFeedback = false
    }
    // One stable row: operation details live in the dialog rather than expanding the footer.
    property bool operationActive: win.networkActivity || lem.saving || !!lem.operationStatus.active
    property string operationSummary: lem.downloadingPhotos ? lem.photoDownloadState : lem.syncing ? (lem.operationStatus.title || lem.syncState) : lem.refreshing ? "A atualizar catálogo · pode continuar a trabalhar" : lem.busy ? (lem.operationStatus.title || "A processar… Por favor aguarde.") : lem.saving ? "A guardar no dispositivo…" : lem.message || "Entre para descarregar os contratos atribuídos."
    ToolBar {
        id: appFooter
        objectName: "appFooter"
        position: ToolBar.Footer
        parent: win.contentItem
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        z: 40
        // Footer visibility must never feed back into the document's end inset.
        implicitHeight: win.touchHeight + topPadding + bottomPadding
        height: implicitHeight
        // ApplicationWindow already keeps this overlay within the system safe area.
        // Override EACH side: padding alone does not replace the style's explicit
        // SafeArea bindings. Moving the toolbar changes its own safe margins;
        // allowing those to resize it creates a height -> Translate -> margin loop.
        topPadding: 4
        bottomPadding: 4
        leftPadding: 4
        rightPadding: 4
        transform: Translate {
            y: appFooter.height * (1 - win.chromeProgress)
        }
        // Slide as an opaque surface: fading would show question text
        // through partially revealed controls and make them hard to read.
        opacity: win.chromeProgress > .001 ? 1 : 0
        visible: win.chromeProgress > .001
        enabled: win.chromeProgress > .98
        clip: true
        padding: 4
        background: Rectangle {
            color: win.operationActive ? win.brandTint : win.softSurface
            border.color: win.operationActive ? win.brandPrimary : win.cardOutline
        }
        contentItem: RowLayout {
            spacing: 6
            UiIconButton {
                id: statusControl
                objectName: "operationStatusButton"
                Layout.preferredWidth: win.touchHeight
                Layout.preferredHeight: win.touchHeight
                primary: win.operationActive
                text: win.operationSummary
                Accessible.name: (win.operationActive ? "Operação em curso. " : "") + "Estado da operação: " + win.operationSummary
                onClicked: {
                    win.showFeedback = false;
                    statusDialog.open();
                }
                contentItem: UiIcon {
                    id: operationGlyph
                    objectName: "operationStatusIcon"
                    kind: "sync"
                    implicitWidth: 26
                    implicitHeight: 26
                    ink: win.operationActive ? "#ffffff" : win.brandPrimary
                    NumberAnimation on rotation {
                        objectName: "operationStatusAnimation"
                        from: 0
                        to: 360
                        duration: 1100
                        loops: Animation.Infinite
                        running: win.operationActive && statusControl.visible && !win.splashActive
                        onRunningChanged: if (!running)
                            operationGlyph.rotation = 0
                    }
                }
            }
            UiLabel {
                objectName: "operationStatus"
                visible: !win.compact
                text: win.operationSummary
                elide: Text.ElideRight
                maximumLineCount: 1
                Layout.fillWidth: true
            }
            UiButton {
                objectName: "syncButton"
                Layout.fillWidth: win.compact
                text: "Sinc. (" + lem.pendingReports + ")"
                Accessible.name: "Sincronizar. " + lem.pendingReports + " reports pendentes"
                enabled: !lem.busy && !lem.syncing && !!lem.user.id
                onClicked: lem.sync()
            }
            UiButton {
                objectName: "reviewReportButton"
                visible: win.page === "report"
                Layout.fillWidth: win.compact
                primary: win.editing
                text: win.editing ? (win.compact ? "Rever" : "Rever report") : "Resumo"
                enabled: !!win.reportData.id && !lem.busy
                onClicked: win.openReview()
            }
        }
    }
    // Follow the visible footer edge, including intermediate animation frames.
    // Both controls and messages retain separate hit areas without resizing content.
    Rectangle {
        id: feedbackToast
        objectName: "feedbackToast"
        parent: win.contentItem
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8 + appFooter.height * win.chromeProgress
        width: Math.min(parent.width - 16, 560)
        implicitHeight: feedbackRow.implicitHeight + 12
        height: implicitHeight
        visible: win.showFeedback && !win.operationActive && !win.splashActive
        z: 41
        radius: 8
        color: "#e2f2ec"
        border.color: "#4e9987"
        MouseArea {
            anchors.fill: parent
            onClicked: {
                win.showFeedback = false;
                statusDialog.open();
            }
        }
        RowLayout {
            id: feedbackRow
            anchors.fill: parent
            anchors.margins: 6
            UiLabel {
                text: lem.message
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            UiIconButton {
                objectName: "dismissFeedbackButton"
                symbol: "close"
                text: "Fechar mensagem"
                Accessible.name: text
                onClicked: win.showFeedback = false
            }
        }
    }
    Dialog {
        // Operation progress uses its own notification channel, separate from report model changes.
        id: statusDialog
        objectName: "statusDialog"
        title: "Estado da operação"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 520)
        height: Math.min(win.height - 32, 600)
        palette: lightPalette
        background: Rectangle {
            color: "white"
            radius: 8
            border.color: win.outline
        }
        contentItem: ColumnLayout {
            ScrollView {
                id: statusScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: statusScroll.availableWidth
                    spacing: 12
                    UiLabel {
                        objectName: "operationDetailTitle"
                        text: lem.operationStatus.title || "Sem operações em curso"
                        font.bold: true
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    ProgressBar {
                        objectName: "operationProgressBar"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 14
                        palette: lightPalette
                        from: 0
                        to: 1
                        value: lem.operationStatus.progress || 0
                        indeterminate: !!lem.operationStatus.indeterminate
                        Accessible.name: "Progresso da operação"
                    }
                    UiLabel {
                        objectName: "operationProgressText"
                        text: lem.syncing ? (lem.operationStatus.reportsDone || 0) + " / " + (lem.operationStatus.reportsTotal || 0) + " reports processados neste lote" : lem.operationStatus.active ? lem.operationStatus.indeterminate ? "A processar · o total ainda não é conhecido" : Math.round((lem.operationStatus.progress || 0) * 100) + "%" : "Sem transferências em curso"
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        color: win.mutedInk
                    }
                    Repeater {
                        model: statusDialog.visible ? lem.operationTransfers.length : 0
                        ColumnLayout {
                            required property int index
                            property var modelData: lem.operationTransfers[index] || {}
                            Layout.fillWidth: true
                            spacing: 4
                            UiLabel {
                                text: modelData.title + (modelData.context ? " · " + modelData.context : "")
                                font.bold: true
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            ProgressBar {
                                Layout.fillWidth: true
                                palette: lightPalette
                                from: 0
                                to: 1
                                value: modelData.progress || 0
                                indeterminate: !!modelData.indeterminate
                                Accessible.name: modelData.title
                            }
                            UiLabel {
                                text: (modelData.direction === "upload" ? "Enviado: " : "Recebido: ") + win.transferSize(modelData.done || 0) + (modelData.total > 0 ? " / " + win.transferSize(modelData.total) : " · total desconhecido") + (modelData.waiting ? "\nA aguardar confirmação/processamento no servidor…" : "")
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                                color: win.mutedInk
                            }
                        }
                    }
                    UiLabel {
                        text: lem.message || "Sem operações recentes."
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    UiLabel {
                        visible: lem.saving
                        text: lem.localSaveError ? "Respostas ainda não guardadas · a tentar novamente" : "A gravar respostas no dispositivo…"
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    UiLabel {
                        objectName: "pendingReportsLabel"
                        text: lem.pendingReports + " report(s) por sincronizar"
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
            UiButton {
                text: "Fechar"
                Layout.fillWidth: true
                onClicked: statusDialog.close()
            }
        }
    }
    Dialog {
        id: groupDialog
        objectName: "groupDialog"
        title: "Grupos do report"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 520)
        height: Math.min(win.height - 32, 620)
        palette: lightPalette
        background: Rectangle {
            color: "white"
            radius: 12
            border.color: win.outline
        }
        contentItem: ColumnLayout {
            ScrollView {
                id: groupListScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: groupListScroll.availableWidth
                    spacing: 8
                    Repeater {
                        model: lem.sections
                        UiButton {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            text: modelData.id + " • " + win.sectionAnswered(modelData) + "/" + modelData.items.length + " • " + win.groupMissing(modelData) + " por validar"
                            Accessible.name: modelData.title + ", " + win.groupMissing(modelData) + " itens por validar"
                            checked: win.selectedSection === index
                            onClicked: {
                                win.selectSection(index);
                                groupDialog.close();
                            }
                        }
                    }
                }
            }
            UiButton {
                text: "Fechar"
                Layout.fillWidth: true
                onClicked: groupDialog.close()
            }
        }
    }
    Dialog {
        id: reviewDialog
        objectName: "reviewDialog"
        title: win.editing ? "Rever antes de concluir" : "Resumo do report"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 600)
        height: Math.min(win.height - 32, 680)
        palette: lightPalette
        background: Rectangle {
            color: "white"
            radius: 12
            border.color: win.outline
        }
        contentItem: ColumnLayout {
            spacing: 12
            UiLabel {
                objectName: "reviewSummary"
                text: win.totalAnswered() + " / " + win.totalItems() + " respostas • " + win.pendingItems().length + " itens por validar"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                font.bold: true
            }
            UiLabel {
                visible: win.editing
                text: win.pendingItems().length ? "Toque num item para preencher o que falta." : "Respostas, observações NOK e fotografias obrigatórias verificadas. Pode concluir o report."
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            ScrollView {
                id: reviewListScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: reviewListScroll.availableWidth
                    spacing: 8
                    Repeater {
                        model: win.pendingItems()
                        Rectangle {
                            required property var modelData
                            objectName: "reviewCard_" + modelData.id
                            Layout.fillWidth: true
                            implicitHeight: reviewItem.implicitHeight + 20
                            color: "#fff4df"
                            radius: 8
                            border.color: "#b98a39"
                            ColumnLayout {
                                id: reviewItem
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 10
                                UiLabel {
                                    text: parent.parent.modelData.id + " • " + parent.parent.modelData.issues
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                    font.bold: true
                                    color: "#714700"
                                }
                                UiLabel {
                                    text: parent.parent.modelData.label
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                UiButton {
                                    text: "Abrir item"
                                    objectName: "reviewJump_" + parent.parent.modelData.id
                                    Layout.fillWidth: true
                                    onClicked: win.jumpTo(parent.parent.modelData.section, parent.parent.modelData.id)
                                }
                            }
                        }
                    }
                }
            }
            UiButton {
                objectName: "finishReportButton"
                text: win.correctingReport ? "Guardar correção e substituir original…" : "Concluir report"
                primary: true
                visible: win.editing
                enabled: !lem.busy && !lem.copyingGroup && !lem.attachingFiles && win.pendingItems().length === 0
                Layout.fillWidth: true
                onClicked: {
                    if (win.correctingReport) saveCorrectionDialog.open();
                    else lem.finish();
                    reviewDialog.close();
                }
            }
            UiButton {
                text: "Voltar ao report"
                Layout.fillWidth: true
                onClicked: reviewDialog.close()
            }
        }
    }
    Dialog {
        id: beginCorrectionDialog
        objectName: "beginCorrectionDialog"
        title: "Corrigir report fechado"
        modal: true
        width: Math.min(560, win.width - 32)
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: lem.beginReportCorrection()
        contentItem: ColumnLayout {
            UiLabel {
                Layout.fillWidth: true
                text: "Será criada uma cópia para editar respostas, observações, evidências e datas. O original continua disponível até guardar a correção completa. A correção conserva o proprietário original."
                wrapMode: Text.Wrap
            }
        }
    }
    Dialog {
        id: saveCorrectionDialog
        objectName: "saveCorrectionDialog"
        title: "Substituir o report original"
        modal: true
        width: Math.min(560, win.width - 32)
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: { if (win.correctingReport && win.editing) lem.finish(); }
        contentItem: ColumnLayout {
            UiLabel {
                Layout.fillWidth: true
                text: "Após a validação no servidor, esta correção substitui o original. Será usado um novo identificador interno. Se a ligação falhar, o envio fica pendente e o original é conservado até confirmação."
                wrapMode: Text.Wrap
            }
        }
    }
    Dialog {
        id: draftSaveErrorDialog
        objectName: "draftSaveErrorDialog"
        property string detail: ""
        title: "Falha ao criar rascunho"
        modal: true
        width: Math.min(480, win.width - 32)
        anchors.centerIn: parent
        contentItem: ColumnLayout {
            UiLabel {
                text: draftSaveErrorDialog.detail
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            UiButton {
                text: "Voltar à lista"
                Layout.fillWidth: true
                onClicked: draftSaveErrorDialog.close()
            }
        }
    }
    Dialog {
        id: correctionDatesDialog
        objectName: "correctionDatesDialog"
        property string saveError: ""
        title: "Data e hora da intervenção"
        modal: true
        width: Math.min(580, win.width - 32)
        anchors.centerIn: parent
        onAboutToShow: saveError = ""
        contentItem: ColumnLayout {
            spacing: 12
            UiLabel { text: "Início da intervenção"; Layout.fillWidth: true }
            UiTextField {
                id: correctionStart
                objectName: "correctionStart"
                Layout.fillWidth: true
                placeholderText: "2026-11-16T09:30:00+00:00"
            }
            UiLabel { text: "Fim da intervenção"; Layout.fillWidth: true }
            UiTextField {
                id: correctionEnd
                objectName: "correctionEnd"
                Layout.fillWidth: true
                placeholderText: "2026-11-16T11:00:00+00:00"
            }
            UiLabel {
                text: "Formato: AAAA-MM-DDTHH:mm:ss+00:00. O fuso é +00:00 no inverno e +01:00 no verão em Portugal continental. Z significa +00:00."
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            UiLabel {
                objectName: "correctionDateSaveError"
                text: correctionDatesDialog.saveError
                visible: text.length > 0
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            UiButton {
                objectName: "saveCorrectionDates"
                text: "Guardar datas"
                Layout.fillWidth: true
                onClicked: {
                    Qt.inputMethod.hide();
                    if (lem.setCorrectionTimes(correctionStart.text, correctionEnd.text))
                        correctionDatesDialog.close();
                    else {
                        win.showFeedback = false;
                        correctionDatesDialog.saveError = lem.message;
                    }
                }
            }
            UiButton { text: "Cancelar"; Layout.fillWidth: true; onClicked: correctionDatesDialog.close() }
        }
    }
    Dialog {
        id: detailsDialog
        title: "Dados do carregador"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 600)
        height: Math.min(win.height - 32, 640)
        palette: lightPalette
        background: Rectangle {
            color: "white"
            radius: 12
            border.color: win.outline
        }
        contentItem: ColumnLayout {
            ScrollView {
                id: detailsScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: detailsScroll.availableWidth
                    spacing: 12
                    UiLabel {
                        text: "Report " + (win.reportData.id || "")
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Repeater {
                        model: win.fields
                        UiLabel {
                            required property var modelData
                            text: modelData.label + ": " + (win.reportData.snapshot ? lem.connectorText(String(win.reportData.snapshot.charger[modelData.key] || "—")) + ((modelData.key === "power" && win.reportData.snapshot.charger.power_unit) ? " " + win.reportData.snapshot.charger.power_unit : "") : "")
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }
                    Repeater {
                        model: win.reportData.snapshot ? Object.keys(win.reportData.snapshot.charger.source || {}) : []
                        UiLabel {
                            required property string modelData
                            text: modelData + ": " + lem.connectorText(String(win.reportData.snapshot.charger.source[modelData]))
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }
                }
            }
            UiButton {
                text: "Fechar"
                Layout.fillWidth: true
                onClicked: detailsDialog.close()
            }
        }
    }
    component ExcelImportPanel: ColumnLayout {
        id: importPanel
        property bool stationsOnly: false
        property string idsContract: ""
        readonly property bool matches: !!lem.importInfo.station_catalogue === stationsOnly
                                        && (lem.importInfo.ids_contract || "") === idsContract
        readonly property var sheets: matches ? (lem.importInfo.sheets || []) : []
        readonly property var preview: matches ? lem.importPreview : ({})
        readonly property var selectedSheet: sheets[sheetBox.currentIndex] || ({})
        readonly property string batch: matches ? (lem.importInfo.batch_id || "") : ""
        property alias previewList: importPreviewList
        property bool confirmInFooter: false
        readonly property bool canConfirm: !lem.busy && preview.count > 0 && !!preview.records
                                           && preview.records.length === preview.count && (preview.errors || []).length === 0
        signal previewRequested(var sheet)
        objectName: idsContract ? "contractIdsImportMappingBody" : stationsOnly ? "stationImportMappingBody" : "importMappingBody"
        visible: sheets.length > 0
        onBatchChanged: if (batch) Qt.callLater(chooseSheet)
        function chooseSheet() {
            let preferred = sheets.findIndex(sheet => !!sheet.recommended_mapping);
            if (preferred < 0)
                preferred = sheets.findIndex(sheet => sheet.name === (lem.importInfo.saved_profile || {}).sheet);
            sheetBox.currentIndex = preferred >= 0 ? preferred : 0;
        }
        UiComboBox {
            id: sheetBox
            implicitHeight: win.touchHeight
            model: importPanel.sheets.map(s => s.name)
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            onActivated: lem.invalidatePreview()
        }
        UiButton {
            implicitHeight: win.touchHeight
            text: "Pré-visualizar importação"
            enabled: !lem.busy && !!importPanel.selectedSheet.name
            objectName: importPanel.idsContract ? "contractIdsPreviewImportButton" : importPanel.stationsOnly ? "previewStationImportButton" : "previewImportButton"
            onClicked: importPanel.previewRequested(importPanel.selectedSheet)
        }
        UiLabel {
            text: importPanel.preview.count !== undefined ? importPanel.preview.count + " IDs válidos • " + (importPanel.preview.errors || []).length + " erros" : ""
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.minimumWidth: 0
        }
        UiLabel {
            visible: importPanel.preview.count > 0 && !importPanel.preview.records
            text: "Atualize o servidor para apresentar todos os carregadores antes de importar."
            color: win.danger
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        ListView {
            id: importPreviewList
            objectName: importPanel.idsContract ? "contractIdsImportPreviewList" : importPanel.stationsOnly ? "stationImportPreviewList" : "importPreviewList"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredHeight: Math.min(640, Math.max(240, count * 180))
            visible: count > 0
            clip: true
            spacing: 12
            reuseItems: true
            model: importPanel.preview.records || []
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                id: previewCard
                required property var modelData
                required property int index
                width: ListView.view.width
                implicitHeight: previewBody.implicitHeight + 24
                color: win.pageSurface
                radius: 8
                border.color: win.cardOutline
                ColumnLayout {
                    id: previewBody
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 12
                    spacing: 10
                    UiLabel {
                        text: (previewCard.index + 1) + " / " + importPreviewList.count + " • " + previewCard.modelData.external_id
                        textFormat: Text.PlainText
                        font.bold: true
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    GridLayout {
                        columns: win.compact ? 1 : 2
                        columnSpacing: 18
                        rowSpacing: 8
                        Layout.fillWidth: true
                        Repeater {
                            model: win.previewDetails(previewCard.modelData)
                            ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 2
                                UiLabel {
                                    text: parent.modelData.label
                                    textFormat: Text.PlainText
                                    font.bold: true
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                UiLabel {
                                    text: parent.modelData.value || "—"
                                    textFormat: Text.PlainText
                                    wrapMode: Text.WrapAnywhere
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
            }
            onModelChanged: positionViewAtBeginning()
        }
        Repeater {
            model: importPanel.preview.errors || []
            UiLabel {
                required property var modelData
                text: "Linha " + modelData.row + ": " + modelData.error + " (" + modelData.id + ")"
                textFormat: Text.PlainText
                color: win.danger
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }
        }
        UiButton {
            primary: true
            implicitHeight: win.touchHeight
            text: "Confirmar importação"
            objectName: importPanel.confirmInFooter ? "" : "confirmImportButton"
            visible: !importPanel.confirmInFooter
            enabled: importPanel.canConfirm
            onClicked: lem.commitExcel()
        }
    }
    Dialog {
        id: contractIdsDialog
        objectName: "contractIdsDialog"
        popupType: Popup.Item
        property string contractId: ""
        readonly property bool valid: lem.authenticated && lem.user.role === "admin" && !!contractId && contractBox.selectedId === contractId
        onValidChanged: if (visible && !valid) close()
        onAboutToShow: {
            contractIdSearch.clear();
            if (win.canManagePcves)
                lem.refreshStations();
        }
        title: "IDs do contrato"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 620)
        height: Math.min(win.height - win.SafeArea.margins.top - win.SafeArea.margins.bottom - 24, 640)
        contentItem: ColumnLayout {
            UiLabel {
                text: contractBox.selectedName
                textFormat: Text.PlainText
                font.bold: true
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            UiButton {
                objectName: "importContractIds"
                text: "Adicionar ou atualizar por Excel"
                enabled: contractIdsDialog.valid && !lem.busy
                Layout.fillWidth: true
                onClicked: pcveImportDialog.start(contractIdsDialog.contractId)
            }
            UiTextField {
                id: contractIdSearch
                objectName: "contractIdSearch"
                placeholderText: "Pesquisar um ID…"
                Accessible.name: "Pesquisar IDs do contrato"
                Layout.fillWidth: true
            }
            ListView {
                id: managedIds
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: contractIdsDialog.visible && contractIdsDialog.valid
                    ? lem.chargers.filter(row => row.external_id.toLowerCase().includes(contractIdSearch.text.trim().toLowerCase())) : []
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    required property var modelData
                    width: managedIds.width
                    UiLabel {
                        text: modelData.external_id
                        textFormat: Text.PlainText
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                    }
                    UiButton {
                        readonly property var station: win.canManagePcves
                            ? win.pcveRecords.find(row => row.id === modelData.station_id) : null
                        text: "Editar"
                        Accessible.name: "Editar " + modelData.external_id
                        objectName: "editContractPcve_" + modelData.id
                        visible: win.canManagePcves
                        enabled: !lem.busy && !!(station || {}).updated
                        onClicked: pcveEditDialog.begin(station)
                    }
                    UiButton {
                        text: "Retirar"
                        Accessible.name: "Retirar " + modelData.external_id
                        objectName: "detachPcve_" + modelData.id
                        enabled: !lem.busy && !lem.refreshing
                        onClicked: {
                            detachPcveDialog.contractId = contractIdsDialog.contractId;
                            detachPcveDialog.chargerId = modelData.id;
                            detachPcveDialog.pcveId = modelData.external_id;
                            detachPcveDialog.open();
                        }
                    }
                }
                UiLabel {
                    visible: managedIds.count === 0
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: lem.refreshing ? "A carregar IDs…" : contractIdSearch.text.trim() ? "Nenhum ID corresponde à pesquisa." : "Sem IDs associados ao contrato."
                }
            }
            UiButton { text: "Fechar"; Accessible.name: "Fechar gestão de IDs"; Layout.fillWidth: true; onClicked: contractIdsDialog.close() }
        }
    }
    Dialog {
        id: detachPcveDialog
        objectName: "detachPcveDialog"
        popupType: Popup.Item
        property string contractId
        property string chargerId
        property string pcveId
        title: "Retirar PCVE do contrato?"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 440)
        contentItem: ColumnLayout {
            UiLabel {
                text: "O PCVE " + detachPcveDialog.pcveId + " continuará na base de dados. Os relatórios existentes serão conservados."
                textFormat: Text.PlainText
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            RowLayout {
                UiButton { text: "Cancelar"; onClicked: detachPcveDialog.reject() }
                UiButton {
                    text: "Retirar"
                    objectName: "confirmDetachPcve"
                    onClicked: {
                        lem.detachCharger(detachPcveDialog.contractId, detachPcveDialog.chargerId);
                        detachPcveDialog.accept();
                    }
                }
            }
        }
    }
    component PcveField: Item {
        id: fieldRow
        required property var modelData
        Layout.fillWidth: true
        implicitHeight: Math.max(fieldLabel.implicitHeight, fieldValue.implicitHeight)
        UiLabel {
            id: fieldLabel
            text: fieldRow.modelData.label
            color: win.mutedInk
            wrapMode: Text.Wrap
            width: fieldRow.width * .42
        }
        UiLabel {
            id: fieldValue
            text: fieldRow.modelData.value
            wrapMode: Text.Wrap
            anchors.left: fieldLabel.right
            anchors.leftMargin: 12
            anchors.right: parent.right
        }
    }
    Dialog {
        id: pcveDetailsDialog
        objectName: "pcveDetailsDialog"
        property string stationId: ""
        readonly property var station: stationId ? win.pcveRecords.find(row => row.id === stationId) : null
        readonly property var details: (station || {}).details || {}
        readonly property var currentFields: visible ? win.pcveFields(details) : []
        readonly property var history: lem.stationReports && lem.stationReports.id === stationId ? lem.stationReports : ({})
        property real restoreY: -1
        function restoreHistoryPosition() {
            if (restoreY < 0 || !opened || !history.rows)
                return;
            // The history arrives after the popup opens. Lay out the report
            // buttons before clamping the saved offset to the new content height.
            pcveDetailsColumn.ensurePolished();
            const view = pcveDetailsScroll.contentItem;
            const maximum = Math.max(0, view.contentHeight - view.height);
            view.contentY = Math.min(restoreY, maximum);
            restoreY = -1;
        }
        onHistoryChanged: {
            if (restoreY >= 0 && history.rows)
                Qt.callLater(restoreHistoryPosition);
        }
        onOpened: lem.loadStationReports(stationId)
        onStationChanged: {
            if (!station && visible)
                Qt.callLater(function () {
                    if (!pcveDetailsDialog.station)
                        pcveDetailsDialog.close();
                });
        }
        onClosed: {
            if (win.pendingReportOpen && win.pendingReportOpen.origin)
                win.pendingReportOpen = null;
            restoreY = -1;
            stationId = "";
            lem.loadStationReports("");
            // A cached report can open inside the popup button's own click.
            // Return key focus to the report after the popup releases its grab.
            if (win.androidCamera && win.page === "report")
                Qt.callLater(function () {
                    if (win.page === "report" && !pcveDetailsDialog.visible)
                        reportScroll.forceActiveFocus(Qt.OtherFocusReason);
                });
        }
        onAboutToShow: pcveDetailsScroll.contentItem.contentY = 0
        title: "Dados do PCVE"
        modal: true
        // The Android overlay can retain its previous size after a density change.
        parent: win.contentItem
        anchors.centerIn: parent
        width: Math.max(0, Math.min(parent.width - 24, 580))
        height: Math.max(0, Math.min(parent.height - 24, 600))
        palette: lightPalette
        background: Rectangle {
            color: "white"
            radius: 12
            border.color: win.outline
        }
        contentItem: ColumnLayout {
            spacing: 10
            ScrollView {
                id: pcveDetailsScroll
                objectName: "pcveDetailsScroll"
                Layout.minimumHeight: 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    id: pcveDetailsColumn
                    width: pcveDetailsScroll.availableWidth
                    spacing: 10
                    UiLabel {
                        objectName: "pcveDetailsId"
                        text: (pcveDetailsDialog.station || {}).external_id || ""
                        textFormat: Text.PlainText
                        font.pixelSize: 22
                        font.bold: true
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Repeater {
                        model: pcveDetailsDialog.currentFields
                        PcveField {}
                    }
                    UiLabel {
                        visible: !pcveDetailsDialog.currentFields.length
                        text: "Sem dados adicionais para este ID."
                        Layout.fillWidth: true
                    }
                    UiLabel {
                        text: "Relatórios associados"
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    UiLabel {
                        text: pcveDetailsDialog.history.message || ""
                        visible: text.length > 0
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Repeater {
                        model: pcveDetailsDialog.history.rows || []
                        UiButton {
                            required property var modelData
                            objectName: "pcveReport_" + modelData.id
                            Layout.fillWidth: true
                            text: modelData.contract + " · " + (modelData.status === "closed" ? "Fechado" : "Rascunho")
                                  + " · " + Qt.formatDateTime(new Date(modelData.started), "dd/MM/yyyy HH:mm:ss") + "\n" + modelData.identifier
                            enabled: !lem.busy
                            implicitHeight: Math.max(win.touchHeight, implicitContentHeight + topPadding + bottomPadding)
                            contentItem: UiLabel {
                                text: parent.text
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                            }
                            onClicked: win.openStationReport(modelData.contract_id, modelData.charger_id, modelData.id)
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                UiButton {
                    objectName: "editPcve"
                    text: "Editar ficha"
                    visible: win.canManagePcves
                    enabled: !lem.busy && !!(pcveDetailsDialog.station || {}).updated
                    primary: true
                    Layout.fillWidth: true
                    onClicked: {
                        pcveEditDialog.begin(pcveDetailsDialog.station);
                        pcveDetailsDialog.close();
                    }
                }
                UiButton {
                    objectName: "closePcveDetails"
                    text: "Fechar"
                    Layout.fillWidth: true
                    onClicked: pcveDetailsDialog.close()
                }
            }
        }
    }
    Dialog {
        id: pcveEditDialog
        objectName: "pcveEditDialog"
        property string stationId: ""
        property string externalId: ""
        property string revision: ""
        property var originalDetails: ({})
        property var edits: ({})
        property bool attempted: false
        readonly property var editResult: lem.stationEditResult || ({})
        readonly property var live: win.pcveRecords.find(row => row.id === stationId) || ({})
        readonly property bool outdated: live.updated !== revision
        readonly property bool saving: attempted && editResult.id === stationId && editResult.state === "saving"
        title: "Editar ficha PCVE"
        modal: true
        closePolicy: saving ? Popup.NoAutoClose : Popup.CloseOnEscape
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 600)
        height: Math.min(win.height - win.SafeArea.margins.top - win.SafeArea.margins.bottom - 24, 680)
        palette: lightPalette
        function begin(station) {
            stationId = station.id;
            externalId = station.external_id;
            revision = station.updated;
            originalDetails = station.details || {};
            edits = {};
            attempted = false;
            editPcveScroll.contentItem.contentY = 0;
            open();
        }
        function originalValue(key) {
            const value = originalDetails[key];
            return value == null ? "" : String(value);
        }
        function setField(key, value) {
            const next = Object.assign({}, edits);
            if (value.trim() === originalValue(key).trim())
                delete next[key];
            else
                next[key] = value;
            edits = next;
        }
        onEditResultChanged: {
            if (attempted && editResult.id === stationId && editResult.state === "saved")
                close();
        }
        onClosed: {
            attempted = false;
            edits = {};
        }
        contentItem: ColumnLayout {
            spacing: 10
            UiLabel {
                text: "ID do PCVE: " + pcveEditDialog.externalId
                textFormat: Text.PlainText
                font.pixelSize: 22
                font.bold: true
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
            }
            ScrollView {
                id: editPcveScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: editPcveScroll.availableWidth
                    spacing: 8
                    Repeater {
                        model: pcveEditDialog.visible ? win.pcveEditFields(pcveEditDialog.originalDetails) : []
                        ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 4
                            UiLabel {
                                text: parent.modelData.label
                                textFormat: Text.PlainText
                                wrapMode: Text.WrapAnywhere
                                Layout.fillWidth: true
                            }
                            UiTextField {
                                objectName: "pcveEdit_" + parent.modelData.key
                                Accessible.name: parent.modelData.label
                                text: pcveEditDialog.originalValue(parent.modelData.key)
                                readOnly: pcveEditDialog.saving
                                selectByMouse: true
                                Layout.fillWidth: true
                                onTextEdited: pcveEditDialog.setField(parent.modelData.key, text)
                            }
                        }
                    }
                }
            }
            UiLabel {
                objectName: "pcveEditStatus"
                text: {
                    if (pcveEditDialog.outdated)
                        return "A ficha foi atualizada. Volte a abrir a edição para rever os dados atuais.";
                    const response = pcveEditDialog.editResult;
                    return pcveEditDialog.attempted && response.id === pcveEditDialog.stationId ? (response.message || "") : "";
                }
                textFormat: Text.PlainText
                visible: text.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: win.danger
            }
            RowLayout {
                Layout.fillWidth: true
                UiButton {
                    text: "Cancelar"
                    enabled: !pcveEditDialog.saving
                    Layout.fillWidth: true
                    onClicked: pcveEditDialog.close()
                }
                UiButton {
                    objectName: "savePcve"
                    text: pcveEditDialog.saving ? "A guardar…" : "Guardar"
                    primary: true
                    enabled: win.canManagePcves && !lem.busy && !pcveEditDialog.outdated && Object.keys(pcveEditDialog.edits).length > 0
                    Layout.fillWidth: true
                    onClicked: {
                        pcveEditDialog.attempted = true;
                        lem.saveStation(pcveEditDialog.stationId, pcveEditDialog.revision, pcveEditDialog.edits);
                    }
                }
            }
        }
    }
    Dialog {
        id: pcveImportDialog
        objectName: "pcveImportDialog"
        popupType: Popup.Item
        property string importNotice: ""
        property bool importStarted: false
        property string contractId: ""
        readonly property bool allowed: contractId ? contractIdsDialog.valid && contractBox.selectedId === contractId : win.canManagePcves
        onAllowedChanged: if (visible && !allowed) close()
        function start(cid) {
            contractId = cid;
            importNotice = "";
            importStarted = cid ? lem.importInfo.ids_contract === cid : !!lem.importInfo.station_catalogue;
            open();
        }
        Connections {
            target: lem
            function onExcelImported(summary) {
                if (pcveImportDialog.visible && pcveImportDialog.importStarted)
                    pcveImportDialog.importNotice = summary;
            }
        }
        function previewSheet(name, row, columns) {
            if (contractId)
                lem.previewContractIds(contractId, name, row, columns);
            else
                lem.previewStationExcel(name, row, columns);
        }
        title: contractId ? "Adicionar ou atualizar IDs" : "Importar IDs de PCVEs"
        modal: true
        closePolicy: lem.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 780)
        height: Math.min(win.height - win.SafeArea.margins.top - win.SafeArea.margins.bottom - 24, 760)
        palette: lightPalette
        contentItem: ColumnLayout {
            spacing: 10
            UiLabel {
                text: pcveImportDialog.contractId
                      ? "Contrato " + contractBox.selectedName + ". O Excel adiciona novos IDs e atualiza os dados dos existentes. Os IDs ausentes da folha continuam associados."
                      : "Abra um Excel no formato Stations ou com ID ou Identificador no cabeçalho. A importação atualiza a ficha usada nos contratos."
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            UiButton {
                objectName: "openPcveExcel"
                text: "Abrir Excel"
                enabled: pcveImportDialog.allowed && !lem.busy
                Layout.fillWidth: true
                onClicked: stationExcelDialog.open()
            }
            UiLabel {
                objectName: "pcveImportStatus"
                text: pcveImportDialog.importNotice || (pcveImportDialog.importStarted ? lem.message : "")
                textFormat: Text.PlainText
                color: win.mutedInk
                visible: text.length > 0
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
            }
            ScrollView {
                id: pcveImportScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: pcveImportScroll.availableWidth
                    ExcelImportPanel {
                        id: globalExcelPanel
                        confirmInFooter: true
                        stationsOnly: !pcveImportDialog.contractId
                        idsContract: pcveImportDialog.contractId
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        onPreviewRequested: sheet => {
                            pcveImportDialog.importNotice = "";
                            win.previewPcveSheet(sheet);
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                UiButton {
                    objectName: pcveImportDialog.contractId ? "contractIdsConfirmImportButton" : "confirmStationImportButton"
                    text: "Confirmar importação"
                    primary: true
                    wrapText: true
                    implicitHeight: Math.max(win.touchHeight, implicitContentHeight + topPadding + bottomPadding)
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    visible: globalExcelPanel.sheets.length > 0
                    enabled: globalExcelPanel.canConfirm
                    onClicked: lem.commitExcel()
                }
                UiButton {
                    text: "Fechar"
                    Accessible.name: "Fechar importação"
                    enabled: !lem.busy
                    Layout.fillWidth: true
                    onClicked: pcveImportDialog.close()
                }
            }
        }
    }
    Dialog {
        id: deleteContractDialog
        objectName: "deleteContractDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 480)
        height: Math.min(win.height - 32, 430)
        modal: true
        title: "Apagar contrato"
        property string contractId: ""
        property string contractName: ""
        onOpened: deleteContractConfirmation.text = ""
        contentItem: ColumnLayout {
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                UiLabel {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: "Apagar o contrato «" + deleteContractDialog.contractName + "»?\n\nOs carregadores, reports e fotografias deixam de estar disponíveis no servidor. Um administrador pode recuperar tudo durante 30 dias. Depois, a eliminação é definitiva.\n\nEscreva APAGAR para confirmar."
                }
            }
            UiTextField {
                id: deleteContractConfirmation
                objectName: "deleteContractConfirmation"
                Layout.fillWidth: true
                placeholderText: "APAGAR"
                Accessible.name: "Escreva APAGAR para confirmar a eliminação do contrato"
                inputMethodHints: Qt.ImhNoPredictiveText
                enabled: !win.networkActivity
            }
            RowLayout {
                Layout.fillWidth: true
                UiButton {
                    text: "Cancelar"
                    Layout.fillWidth: true
                    onClicked: deleteContractDialog.close()
                }
                UiButton {
                    objectName: "confirmDeleteContract"
                    text: "Apagar contrato"
                    primary: true
                    selectedColor: win.danger
                    Layout.fillWidth: true
                    enabled: lem.authenticated && lem.user.role === "admin" && !win.networkActivity && deleteContractConfirmation.text === "APAGAR"
                    onClicked: {
                        lem.deleteContract(deleteContractDialog.contractId, deleteContractConfirmation.text);
                        deleteContractDialog.close();
                    }
                }
            }
        }
    }
    Dialog {
        id: deletedContractsDialog
        objectName: "deletedContractsDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 540)
        height: Math.min(win.height - 32, 560)
        modal: true
        title: "Contratos apagados"
        contentItem: ColumnLayout {
            UiLabel {
                text: "Recuperação disponível durante 30 dias. Os reports, fotografias e atribuições aos técnicos são recuperados com o contrato."
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            ScrollView {
                id: deletedContractsScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: deletedContractsScroll.availableWidth
                    spacing: 12
                    UiLabel {
                        text: lem.busy ? "A carregar contratos apagados…" : "Sem contratos para recuperar."
                        visible: !(lem.deletedContracts || []).length
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }
                    Repeater {
                        model: lem.deletedContracts || []
                        ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            UiLabel {
                                text: parent.modelData.name
                                font.bold: true
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                            }
                            UiLabel {
                                text: "Recuperar antes de: " + new Date(parent.modelData.purge_after).toLocaleString(Qt.locale("pt_PT"), "dd/MM/yyyy HH:mm")
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                            }
                            UiButton {
                                objectName: "restoreContract_" + parent.modelData.id
                                text: "Recuperar contrato"
                                enabled: lem.authenticated && lem.user.role === "admin" && !win.networkActivity
                                onClicked: lem.restoreContract(parent.modelData.id)
                            }
                        }
                    }
                }
            }
            UiButton {
                text: "Fechar"
                Layout.fillWidth: true
                onClicked: deletedContractsDialog.close()
            }
        }
    }
    Dialog {
        id: photoDownloadDialog
        objectName: "photoDownloadDialog"
        title: "Fotografias para usar sem rede"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 460)
        height: Math.min(win.height - 32, 460)
        property var summary: ({})
        property string reportId: ""
        palette: lightPalette
        background: Rectangle {
            color: "white"
            radius: 8
            border.color: win.outline
        }
        contentItem: ColumnLayout {
            spacing: 12
            ScrollView {
                id: downloadInfoScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                UiLabel {
                    width: downloadInfoScroll.availableWidth
                    wrapMode: Text.Wrap
                    text: (photoDownloadDialog.summary.total || 0) + " fotografia(s) no report. Faltam " + (photoDownloadDialog.summary.missing || 0) + " neste dispositivo. " + (photoDownloadDialog.summary.unknown ? "O servidor não indica o tamanho de todas as imagens." : "Tamanho a descarregar: " + ((photoDownloadDialog.summary.bytes || 0) / 1048576).toFixed(1) + " MiB.") + (photoDownloadDialog.summary.unavailable ? " Há fotografias locais em falta que nunca foram enviadas e não podem ser recuperadas do servidor." : "") + " As imagens já guardadas não serão repetidas. Este report ficará mantido offline, fora da limpeza automática, até escolher Permitir limpeza destas fotografias no menu. Pode continuar a trabalhar durante o descarregamento."
                }
            }
            UiButton {
                text: "Cancelar"
                Layout.fillWidth: true
                onClicked: photoDownloadDialog.close()
            }
            UiButton {
                objectName: "confirmDownloadReportPhotos"
                text: "Descarregar todas"
                primary: true
                Layout.fillWidth: true
                enabled: win.reportData.id === photoDownloadDialog.reportId && !lem.downloadingPhotos
                onClicked: {
                    lem.downloadReportPhotos();
                    photoDownloadDialog.close();
                }
            }
        }
    }
    Dialog {
        id: deleteReportDialog
        objectName: "deleteReportDialog"
        title: closedReport ? "Apagar report fechado?" : "Apagar rascunho?"
        property string reportId: ""
        property bool closedReport: false
        property string reportIdentifier: ""
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 440)
        palette: lightPalette
        background: Rectangle {
            color: "white"
            radius: 8
            border.color: win.danger
        }
        contentItem: ColumnLayout {
            spacing: 12
            UiLabel {
                text: "Report " + deleteReportDialog.reportIdentifier
                font.bold: true
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
            }
            UiLabel {
                text: "O report e todas as fotografias associadas serão apagados definitivamente. Esta ação não pode ser desfeita. Os originais na galeria mantêm-se."
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            UiLabel {
                text: "Para reports já enviados, é necessária ligação ao servidor."
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            UiButton {
                objectName: "cancelDeleteReport"
                text: "Cancelar"
                Layout.fillWidth: true
                onClicked: deleteReportDialog.close()
            }
            UiButton {
                objectName: "confirmDeleteReport"
                text: "Apagar definitivamente"
                destructive: true
                Layout.fillWidth: true
                enabled: win.canDeleteReport && win.reportData.id === deleteReportDialog.reportId && !win.reportNetworkActivity
                onClicked: {
                    const id = deleteReportDialog.reportId;
                    deleteReportDialog.close();
                    lem.deleteReport(id);
                }
            }
        }
    }
    Dialog {
        // Capture the report ID before confirmation so navigation cannot transfer another report.
        id: transferDialog
        objectName: "transferDialog"
        title: "Transferir propriedade"
        property string reportId: ""
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 460)
        contentItem: ColumnLayout {
            UiLabel {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "Só o novo proprietário poderá editar ou concluir. Alterações offline do proprietário anterior não serão aplicadas a este report. A transferência fica registada no servidor."
            }
            UiComboBox {
                id: ownerBox
                objectName: "ownerBox"
                model: lem.users
                textRole: "label"
                Layout.fillWidth: true
                enabled: !lem.busy
            }
            UiButton {
                text: "Cancelar"
                Layout.fillWidth: true
                onClicked: transferDialog.close()
            }
            UiButton {
                objectName: "confirmTransferReport"
                text: "Confirmar transferência"
                primary: true
                Layout.fillWidth: true
                enabled: !win.reportNetworkActivity && win.reportData.id === transferDialog.reportId && ownerBox.currentIndex >= 0 && lem.users[ownerBox.currentIndex].id !== win.reportData.owner_id
                onClicked: {
                    const owner = lem.users[ownerBox.currentIndex].id;
                    transferDialog.close();
                    lem.transferReport(owner);
                }
            }
        }
    }
    Dialog {
        id: copyReportDialog
        objectName: "copyReportDialog"
        title: "Criar novo rascunho?"
        property string reportId: ""
        onOpened: reportId = lem.current.id
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 460)
        contentItem: ColumnLayout {
            UiLabel {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "Será criado um report independente em seu nome, com novas datas, as respostas, observações e fotografias desta cópia local. Ficará em rascunho. O original será conservado. Se faltarem fotografias, descarregue-as primeiro."
            }
            UiButton {
                text: "Cancelar"
                Layout.fillWidth: true
                onClicked: copyReportDialog.close()
            }
            UiButton {
                objectName: "confirmCopyReport"
                text: "Criar cópia local"
                primary: true
                Layout.fillWidth: true
                enabled: !win.reportNetworkActivity && lem.current.id === copyReportDialog.reportId
                onClicked: {
                    copyReportDialog.close();
                    lem.copyLocalReport();
                }
            }
        }
    }
    Dialog {
        // Read-only server comparison; _conflict_remote does not replace the local payload.
        id: serverVersionDialog
        objectName: "serverVersionDialog"
        title: "Versão do servidor · só leitura"
        property var remote: win.reportData._conflict_remote || {}
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 600)
        height: Math.min(win.height - 32, 660)
        contentItem: ColumnLayout {
            UiLabel {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: lem.busy ? "A consultar o servidor…" : serverVersionDialog.remote.id ? "Proprietário: " + serverVersionDialog.remote.technician + " · " + serverVersionDialog.remote.status + " · revisão " + serverVersionDialog.remote.revision + " · " + (serverVersionDialog.remote.photos || []).length + " fotografia(s)" : "A versão do servidor não está disponível. Consulte o estado da operação."
            }
            ScrollView {
                id: serverVersionScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: serverVersionScroll.availableWidth
                    spacing: 12
                    Repeater {
                        model: lem.sections
                        ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            UiLabel {
                                text: modelData.title
                                font.bold: true
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            Repeater {
                                model: modelData.items
                                UiLabel {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    property var answer: ((serverVersionDialog.remote.payload || {}).answers || {})[modelData.id] || {}
                                    text: modelData.id + " · " + modelData.label + "\n" + (answer.value || "Por preencher") + (answer.notes ? "\nObs.: " + answer.notes : "")
                                }
                            }
                        }
                    }
                }
            }
            UiLabel {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "A cópia local e as suas fotografias mantêm-se intactas."
            }
            UiButton {
                text: "Fechar"
                Layout.fillWidth: true
                onClicked: serverVersionDialog.close()
            }
        }
    }
    Dialog {
        id: removePhotoDialog
        objectName: "removePhotoDialog"
        title: "Remover fotografia?"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 440)
        property string photoId: ""
        property string reportId: ""
        property string checkpoint: ""
        palette: lightPalette
        background: Rectangle {
            color: "white"
            radius: 8
            border.color: win.outline
        }
        contentItem: ColumnLayout {
            spacing: 12
            UiLabel {
                text: "Remover a fotografia do item " + removePhotoDialog.checkpoint + "? O original na galeria é conservado. Se já foi enviada, a remoção será aplicada no servidor na próxima sincronização."
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            UiButton {
                text: "Cancelar"
                objectName: "cancelRemovePhoto"
                Layout.fillWidth: true
                onClicked: removePhotoDialog.close()
            }
            UiButton {
                text: "Remover evidência"
                objectName: "confirmRemovePhoto"
                primary: true
                Layout.fillWidth: true
                enabled: win.editing && !lem.busy && win.reportData.id === removePhotoDialog.reportId

                onClicked: {
                    lem.removePhoto(removePhotoDialog.photoId);
                    removePhotoDialog.close();
                }
            }
        }
    }
    Dialog {
        id: photoPreview
        onClosed: photoSource = ""
        title: "Evidência fotográfica"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 900)
        height: Math.min(win.height - 32, 720)
        property url photoSource: ""
        palette: lightPalette
        background: Rectangle {
            color: "white"
            radius: 8
            border.color: win.outline
        }
        contentItem: ColumnLayout {
            Image {
                source: photoPreview.photoSource
                // Large previews are transient; thumbnails keep their own cache.
                cache: false
                Layout.fillWidth: true
                Layout.fillHeight: true
                fillMode: Image.PreserveAspectFit
                autoTransform: true
                asynchronous: true
                sourceSize.width: 1600
                sourceSize.height: 1600
            }
            UiIconButton {
                objectName: "closePhotoPreview"
                symbol: "close"
                text: "Fechar fotografia"
                Layout.alignment: Qt.AlignRight
                onClicked: photoPreview.close()
            }
        }
    }
    Dialog {
        id: clearLocalCopiesDialog
        objectName: "clearLocalCopiesDialog"
        title: "Limpar cópias locais?"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 440)
        palette: lightPalette
        background: Rectangle {
            color: "white"
            radius: 8
            border.color: win.outline
        }
        contentItem: ColumnLayout {
            spacing: 12
            UiLabel {
                text: "Remove deste dispositivo apenas as fotografias e vídeos já sincronizados, incluindo os mantidos offline. Os reports, respostas e histórico permanecem disponíveis na lista e neste dispositivo. Pode voltar a descarregar as evidências do servidor."
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            UiLabel {
                text: "Dados por enviar, rascunhos ainda não publicados, conflitos e originais na galeria são conservados. Esta limpeza não apaga reports nem rascunhos no servidor."
                font.bold: true
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            UiButton {
                objectName: "cancelClearLocalCopies"
                text: "Cancelar"
                Layout.fillWidth: true
                onClicked: clearLocalCopiesDialog.close()
            }
            UiButton {
                objectName: "confirmClearLocalCopies"
                text: "Limpar deste dispositivo"
                destructive: true
                Layout.fillWidth: true
                enabled: !win.networkActivity
                onClicked: {
                    photoPreview.close();
                    photoPreview.photoSource = "";
                    clearLocalCopiesDialog.close();
                    lem.clearLocalCopies();
                }
            }
        }
    }
    Dialog {
        id: scrollDiagnosticDialog
        objectName: "scrollDiagnosticDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Diagnóstico da página · R1"
        width: Math.min(win.width - 24, 600)
        height: Math.min(win.height - 32, 650)
        contentItem: ColumnLayout {
            UiLabel {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "Dados de dimensão capturados antes de abrir Conta. Não inclui respostas, fotografias ou dados da conta."
            }
            ScrollView {
                id: diagnosticScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                TextArea {
                    id: diagnosticText
                    objectName: "diagnosticText"
                    width: diagnosticScroll.availableWidth
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    font.family: "monospace"
                    font.pixelSize: 12
                    text: win.capturedPageDiagnostic
                }
            }
            UiButton {
                objectName: "copyScrollDiagnosticButton"
                Layout.fillWidth: true
                text: "Copiar diagnóstico"
                onClicked: {
                    diagnosticText.selectAll();
                    diagnosticText.copy();
                    diagnosticText.deselect();
                    win.diagnosticCopied = true;
                }
            }
            UiLabel {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: win.diagnosticCopied
                text: "Copiado. Cole o texto na conversa."
            }
            UiButton {
                Layout.fillWidth: true
                text: "Fechar"
                onClicked: scrollDiagnosticDialog.close()
            }
        }
    }
    Dialog {
        id: loginDialog
        objectName: "accountDialog"
        palette: lightPalette
        background: Rectangle {
            color: "#ffffff"
            radius: 8
            border.color: win.outline
        }
        modal: true
        title: "Conta e ligação"
        onClosed: {
            passwordField.clear();
            win.debugTapCount = 0;
            debugTapTimer.stop();
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 440)
        padding: 16
        height: Math.min(win.height - 32, 600)
        contentItem: Flickable {
            id: loginScroll
            // The viewport owns the width on narrow screens and after rotation.
            // The form can grow vertically while labels and fields stay inside it.
            contentWidth: width
            contentHeight: loginForm.implicitHeight
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: ScrollBar {
                id: loginScrollbar
                policy: ScrollBar.AsNeeded
            }
            Column {
                id: loginForm
                width: Math.max(0, loginScroll.width - loginScrollbar.width)
                spacing: 8
                UiLabel {
                    objectName: "interfaceRevisionLabel"
                    visible: win.debugToolsEnabled
                    text: "Interface R1 · " + win.interfaceRevision
                    wrapMode: Text.Wrap
                    width: parent.width
                }
                UiButton {
                    objectName: "openScrollDiagnosticButton"
                    visible: win.debugToolsEnabled
                    text: "Diagnóstico da página"
                    width: parent.width
                    onClicked: {
                        if (!win.capturedPageDiagnostic)
                            win.capturePageDiagnostic();
                        scrollDiagnosticDialog.open();
                    }
                }
                UiLabel {
                    text: "Lem • Versão " + Qt.application.version
                    color: win.mutedInk
                    width: parent.width
                    wrapMode: Text.Wrap
                }
                UiLabel {
                    id: accountVersionStatus
                    objectName: "accountVersionStatus"
                    visible: !!lem.clientCompatibility.message
                    text: lem.clientCompatibility.message || ""
                    color: lem.clientCompatibility.compatible ? win.mutedInk : win.danger
                    font.bold: true
                    wrapMode: Text.Wrap
                    width: parent.width
                }
                UiLabel {
                    id: accountSessionStatus
                    text: lem.authenticated ? "Sessão iniciada: " + (lem.user.name || "") + (lem.user.role === "admin" ? " • Administrador" : " • Técnico") + "\n" + (lem.user.email || "")
                          : lem.sessionPending ? "Sessão guardada · por validar" : "Sem sessão iniciada"
                    font.bold: true
                    wrapMode: Text.Wrap
                    width: parent.width
                }
                UiLabel {
                    id: accountOfflineNote
                    visible: !lem.authenticated && !!lem.user.id
                    text: lem.sessionPending ? "Os dados locais continuam disponíveis. A app valida a sessão quando consegue ligar ao servidor."
                                             : "Existem dados locais desta conta, mas é necessário entrar para comunicar com o servidor."
                    wrapMode: Text.Wrap
                    width: parent.width
                }
                UiLabel {
                    text: "Ligação ao servidor"
                    font.bold: true
                    color: win.brandPrimary
                    topPadding: 12
                }
                UiTextField {
                    id: serverField
                    implicitHeight: win.touchHeight
                    objectName: "serverField"
                    Accessible.name: "Servidor HTTPS"
                    text: lem.server
                    width: parent.width
                    selectByMouse: true
                }
                UiButton {
                    objectName: "defaultServerButton"
                    text: "Usar servidor Lem"
                    width: parent.width
                    enabled: !win.networkActivity
                    onClicked: {
                        win.recordDebugTap();
                        lem.useDefaultServer();
                        serverField.text = lem.server;
                    }
                }
                Column {
                    visible: !lem.authenticated
                    width: parent.width
                    spacing: 5
                    UiLabel {
                        text: "Email"
                    }
                    UiTextField {
                        id: emailField
                        implicitHeight: win.touchHeight
                        Accessible.name: "Email"
                        width: parent.width
                        inputMethodHints: Qt.ImhEmailCharactersOnly
                        selectByMouse: true
                    }
                    UiLabel {
                        text: "Password"
                    }
                    UiTextField {
                        id: passwordField
                        implicitHeight: win.touchHeight
                        Accessible.name: "Password"
                        width: parent.width
                        echoMode: TextInput.Password
                    }
                    UiLabel {
                        text: "Os dados descarregados e os rascunhos ficam disponíveis neste dispositivo sem rede."
                        wrapMode: Text.Wrap
                        width: parent.width
                    }
                    UiButton {
                        primary: true
                        implicitHeight: win.touchHeight
                        text: "Entrar"
                        width: parent.width
                        enabled: !win.networkActivity
                        onClicked: {
                            lem.server = serverField.text;
                            lem.login(emailField.text, passwordField.text);
                            passwordField.clear();
                        }
                    }
                }
                UiButton {
                    implicitHeight: win.touchHeight
                    text: "Ver demonstração local"
                    objectName: "localDemoButton"
                    visible: win.debugToolsEnabled
                    onClicked: {
                        lem.demo();
                        win.page = "report";
                        loginDialog.close();
                    }
                }
                UiButton {
                    implicitHeight: win.touchHeight
                    text: "Terminar sessão"
                    enabled: !win.networkActivity
                    visible: !!lem.user.id
                    onClicked: {
                        lem.logout();
                        loginDialog.close();
                        win.page = "catalog";
                    }
                }
                UiLabel {
                    text: "Dados neste dispositivo"
                    font.bold: true
                    color: win.brandPrimary
                    topPadding: 12
                }
                UiLabel {
                    text: "Fotografias locais: " + lem.photoCacheMB.toFixed(1) + " MB • os rascunhos e envios pendentes são protegidos na limpeza."
                    wrapMode: Text.Wrap
                    width: parent.width
                }
                UiButton {
                    objectName: "clearLocalCopiesButton"
                    text: "Limpar cópias locais"
                    width: parent.width
                    enabled: !win.networkActivity
                    onClicked: clearLocalCopiesDialog.open()
                }
                UiLabel {
                    text: lem.message
                    wrapMode: Text.Wrap
                    width: parent.width
                }
                UiButton {
                    text: "Fechar"
                    width: parent.width
                    onClicked: {
                        Qt.inputMethod.hide();
                        loginDialog.close();
                    }
                }
            }
        }
    }

    // Version notices are separate from transient operation feedback, so catalogue
    // refresh messages cannot erase an update reminder or a blocked-login reason.
    property string versionNoticeKey: ""
    Dialog {
        id: versionDialog
        objectName: "versionDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 440)
        modal: true
        title: lem.clientCompatibility.compatible ? "Atualização disponível" : "Atualização necessária"
        ColumnLayout {
            width: parent.width
            UiLabel {
                id: versionNoticeLabel
                objectName: "versionNotice"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: lem.clientCompatibility.message || ""
            }
            UiButton {
                objectName: "versionDownloadButton"
                text: "Como atualizar"
                visible: !!lem.clientCompatibility.update_url
                Layout.fillWidth: true
                onClicked: Qt.openUrlExternally(lem.clientCompatibility.update_url)
            }
            UiButton {
                text: lem.clientCompatibility.compatible ? "Continuar" : "Fechar"
                Layout.fillWidth: true
                onClicked: versionDialog.close()
            }
        }
    }
    Connections {
        target: lem
        function onCompatibilityChanged() {
            const policy = lem.clientCompatibility;
            const key = JSON.stringify(policy);
            if (key === win.versionNoticeKey)
                return;
            win.versionNoticeKey = key;
            if (policy.message && (!policy.compatible || policy.update_available))
                versionDialog.open();
        }
    }
    // Reconcile backend feedback with navigation state; dialogs never own durable report state.
    Connections {
        target: lem
        function onChargersChanged() {
            win.refreshCatalog(false);
        }
        function onChanged() {
            if (!lem.authenticated || lem.user.role !== "admin") {
                deleteContractDialog.close();
                deletedContractsDialog.close();
                newPassword.clear();
            }
            if (!win.canManagePcves)
                pcveEditDialog.close();
            const pending = win.pendingReportOpen;
            if (pending && !lem.busy) {
                win.pendingReportOpen = null;
                if (lem.contractId() === pending.contractId &&
                    (!pending.origin || (pcveDetailsDialog.visible && pcveDetailsDialog.stationId === pending.origin.stationId))) {
                    if (lem.current.id === pending.id) {
                        win.stationReportOrigin = pending.origin;
                        win.page = "report";
                    } else if (pending.origin) {
                        lem.loadStationReports(pending.origin.stationId);
                    }
                }
            }
            if (win.page === "report" && !lem.current.id && !lem.busy)
                win.leaveReport();
        }
    }
    PhotoCropDialog {
        id: cropDialog
        safeTop: win.SafeArea.margins.top
        safeBottom: win.SafeArea.margins.bottom
        safeLeft: win.SafeArea.margins.left
        safeRight: win.SafeArea.margins.right
        parent: Overlay.overlay
        canApply: lem.current.id === reportId && win.editing && !lem.busy && !lem.syncing
                  && !lem.refreshing && !lem.attachingFiles && !lem.currentReportDownloadingPhotos
        onApplyEdit: (reportId, photoId, quarterTurns, x, y, width, height) => {
            if (canApply && lem.editPhoto(photoId, quarterTurns, x, y, width, height))
                cropDialog.close();
        }
    }
    FileDialog {
        id: videoDialog
        title: "Adicionar vídeos · até 120 segundos e 60 MiB por vídeo"
        nameFilters: ["Vídeos (*.mp4 *.3gp)"]
        fileMode: FileDialog.OpenFiles
        onAccepted: lem.attachMany(win.photoItem, selectedFiles)
    }
    FileDialog {
        id: signaturePhotoDialog
        property string checkpoint: ""
        title: "Anexar fotografia da assinatura"
        nameFilters: ["Fotografias (*.jpg *.jpeg *.png)"]
        fileMode: FileDialog.OpenFile
        onAccepted: lem.importSignature(checkpoint, selectedFile)
    }
    FileDialog {
        id: photoDialog
        title: "Adicionar fotografias"
        nameFilters: ["Fotografias (*.jpg *.jpeg *.png *.heic)"]
        fileMode: FileDialog.OpenFiles
        onAccepted: lem.attachMany(win.photoItem, selectedFiles)
    }
    FileDialog {
        id: excelDialog
        title: "Importar carregadores"
        nameFilters: ["Excel (*.xlsx *.xlsm)"]
        onAccepted: lem.analyseExcel(selectedFile)
    }
    FileDialog {
        id: stationExcelDialog
        title: "Importar IDs de PCVEs"
        nameFilters: ["Excel (*.xlsx *.xlsm)"]
        onAccepted: {
            pcveImportDialog.importNotice = "";
            pcveImportDialog.importStarted = true;
            if (pcveImportDialog.contractId && pcveImportDialog.allowed)
                lem.analyseContractIds(selectedFile, pcveImportDialog.contractId);
            else if (!pcveImportDialog.contractId && pcveImportDialog.allowed)
                lem.analyseStationExcel(selectedFile);
        }
    }
    FileDialog {
        id: exportDialog
        title: "Guardar report"
        fileMode: FileDialog.SaveFile
        nameFilters: win.exportKind === "pdf" ? ["PDF (*.pdf)"] : ["Report e evidências (*.zip)"]
        onAccepted: lem.exportReport(win.exportKind, selectedFile)
    }
    FolderDialog {
        id: contractExportFolder
        property string contractId: ""
        property string kind: "pdf"
        title: contractExportFolder.kind === "pdf" ? "Pasta para todos os PDFs do contrato" : "Pasta para todos os ZIPs do contrato"
        onAccepted: lem.exportContract(contractExportFolder.contractId, contractExportFolder.kind, contractExportFolder.selectedFolder)
    }
    Dialog {
        id: resetPasswordDialog
        objectName: "resetPasswordDialog"
        property string userId: ""
        property string userLabel: ""
        title: "Repor password do técnico"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 480)
        palette: lightPalette
        onOpened: { resetPassword.clear(); repeatPassword.clear(); }
        onClosed: { resetPassword.clear(); repeatPassword.clear(); }
        contentItem: ColumnLayout {
            spacing: 10
            UiLabel {
                text: resetPasswordDialog.userLabel
                font.bold: true
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
            }
            UiLabel {
                text: "As sessões atuais deste técnico serão terminadas. Os reports guardados nos dispositivos permanecem disponíveis."
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            UiTextField {
                id: resetPassword
                objectName: "resetTechnicianNewPassword"
                placeholderText: "Nova password (12 a 200 caracteres)"
                Accessible.name: "Nova password do técnico"
                echoMode: TextInput.Password
                maximumLength: 200
                Layout.fillWidth: true
            }
            UiTextField {
                id: repeatPassword
                objectName: "resetTechnicianRepeatPassword"
                placeholderText: "Repetir nova password"
                Accessible.name: "Repetir nova password do técnico"
                echoMode: TextInput.Password
                maximumLength: 200
                Layout.fillWidth: true
            }
            UiLabel {
                text: "As passwords não coincidem."
                visible: repeatPassword.text.length > 0 && resetPassword.text !== repeatPassword.text
                color: win.danger
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            UiButton {
                text: "Cancelar"
                Layout.fillWidth: true
                onClicked: resetPasswordDialog.close()
            }
            UiButton {
                objectName: "confirmResetTechnicianPassword"
                text: "Confirmar nova password"
                primary: true
                Layout.fillWidth: true
                enabled: lem.authenticated && lem.user.role === "admin" && !lem.busy
                         && resetPassword.text.length >= 12 && resetPassword.text === repeatPassword.text
                         && lem.users.some(u => u.id === resetPasswordDialog.userId && u.role === "tech")
                onClicked: {
                    if (lem.resetTechnicianPassword(resetPasswordDialog.userId, resetPassword.text))
                        resetPasswordDialog.close();
                }
            }
        }
    }
    Item {
        id: pageViewport
        clip: true
        anchors.fill: parent
        // Keep horizontal breathing room, but let content scroll right up to
        // the fixed header/footer edges. Vertical margins used to leave pale
        // strips on screen even when their overlaid controls were fully hidden.
        anchors.leftMargin: win.pageInset
        anchors.rightMargin: win.pageInset
        ScrollChrome {
            id: contractChrome
            objectName: "contractChrome"
            visible: win.page === "catalog" || win.page === "admin"
            RowLayout {
                Layout.fillWidth: true
                UiComboBox {
                    id: contractBox
                    // ComboBox can highlight its first row before the user has
                    // selected a contract. The controller owns the selection.
                    property string selectedId: lem.contractId()
                    readonly property string selectedName: (lem.contracts.find(c => c.id === selectedId) || {}).name || ""
                    displayText: selectedName || "Escolher contrato"
                    implicitHeight: win.touchHeight
                    objectName: "contractSelector"
                    Accessible.name: "Contrato"
                    Accessible.description: selectedName
                    Layout.fillWidth: true
                    model: lem.contracts
                    textRole: "name"
                    onActivated: {
                        lem.selectContract(model[currentIndex].id);
                        // A contract selection changes the contents of the
                        // active page without navigating away from it.
                    }
                    Connections {
                        target: lem
                        function onChanged() {
                            // Each read of lem.contracts converts the whole list: read once.
                            const contracts = lem.contracts, selected = lem.contractId();
                            contractBox.selectedId = selected;
                            for (let i = 0; i < contracts.length; i++)
                                if (contracts[i].id === selected) {
                                    contractBox.currentIndex = i;
                                    return;
                                }
                            contractBox.currentIndex = -1;
                        }
                    }
                }
                UiButton {
                    implicitHeight: win.touchHeight
                    text: "Atualizar"
                    enabled: !lem.busy
                    onClicked: lem.refresh()
                }
            }
        }
        Item {
            id: catalogPage
            visible: win.page === "catalog"
            anchors.fill: parent
            ChromeBackdrop {}
            ScrollChrome {
                id: catalogChrome
                y: win.pageInset + contractChrome.height + win.pageGap
                objectName: "catalogChrome"
                contentSpacing: 5
                UiLabel {
                    text: "Carregadores do contrato"
                    font.pixelSize: 23
                    font.bold: true
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: 6
                    visible: !!lem.desktopExports
                    UiButton {
                        objectName: "exportContractPdfs"
                        text: "Exportar todos os PDFs…"
                        width: Math.min(implicitWidth, parent.width)
                        enabled: lem.authenticated && contractBox.currentIndex >= 0 && !lem.contractExport.active
                        onClicked: {
                            contractExportFolder.contractId = lem.contractId();
                            contractExportFolder.kind = "pdf";
                            contractExportFolder.open();
                        }
                    }
                    UiButton {
                        objectName: "exportContractZips"
                        text: "Exportar todos os ZIPs…"
                        width: Math.min(implicitWidth, parent.width)
                        enabled: lem.authenticated && contractBox.currentIndex >= 0 && !lem.contractExport.active
                        onClicked: {
                            contractExportFolder.contractId = lem.contractId();
                            contractExportFolder.kind = "bundle";
                            contractExportFolder.open();
                        }
                    }
                }
                UiLabel {
                    visible: !!lem.desktopExports
                    text: "Só reports fechados. É criada uma nova pasta; ficheiros existentes são conservados."
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                UiLabel {
                    objectName: "contractExportStatus"
                    visible: !!lem.desktopExports && !!lem.contractExport.message
                    text: (lem.contractExport.contract_name ? lem.contractExport.contract_name + " · " : "") + (lem.contractExport.message || "")
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                RowLayout {
                    visible: !!lem.desktopExports && !!lem.contractExport.directory
                    Layout.fillWidth: true
                    UiButton {
                        objectName: "cancelContractExport"
                        text: "Cancelar exportação"
                        visible: !!lem.contractExport.active
                        enabled: !lem.contractExport.cancelling
                        onClicked: lem.cancelContractExport()
                    }
                    UiButton {
                        objectName: "openContractExportFolder"
                        text: "Abrir pasta"
                        onClicked: Qt.openUrlExternally(lem.contractExport.directory_url)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    UiTextField {
                        id: search
                        objectName: "catalogSearch"
                        onTextChanged: searchDelay.restart()
                        Timer {
                            id: searchDelay
                            interval: 180
                            onTriggered: win.refreshCatalog(false)
                        }
                        implicitHeight: win.touchHeight
                        placeholderText: "Pesquisar carregadores…"
                        Accessible.name: "Pesquisar por ID, série, morada, modelo ou CPE"
                        Layout.fillWidth: true
                        selectByMouse: true
                        Keys.onEscapePressed: text = ""
                        onAccepted: Qt.inputMethod.hide()
                    }
                    UiIconButton {
                        objectName: "clearCatalogSearch"
                        symbol: "erase"
                        text: "Limpar pesquisa"
                        visible: search.text.length > 0
                        onClicked: {
                            search.clear();
                            search.forceActiveFocus();
                        }
                    }
                }
                UiLabel {
                    visible: chargerList.count === 0
                    text: lem.chargers.length ? "Nenhum carregador corresponde à pesquisa." : "Escolha um contrato. O administrador pode importar o Excel na área Gestão."
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
            ListView {
                id: chargerList
                objectName: "chargerList"
                ScrollBar.vertical: UiPageScrollBar {
                    pageView: chargerList
                }
                anchors.fill: parent
                boundsBehavior: Flickable.StopAtBounds
                boundsMovement: Flickable.StopAtBounds
                header: Item {
                    width: chargerList.width
                    height: win.pageTopInset
                }
                footer: Item {
                    width: chargerList.width
                    height: win.pageBottomInset
                }
                clip: true
                spacing: 10
                // Cards have no editable local state; bindings follow the reused row.
                reuseItems: true
                model: win.catalogRows
                delegate: Rectangle {
                    id: chargerTile
                    required property var modelData
                    objectName: "chargerCard_" + modelData.id
                    // Completion belongs to this contract's report summaries,
                    // including locally closed reports still awaiting upload.
                    readonly property bool completed: (modelData.reports || []).some(r => r.status === "closed")
                    width: chargerList.width
                    height: Math.ceil(chargerCard.implicitHeight) + 24
                    color: completed ? "#edf8f3" : "#fff8ed"
                    radius: 8
                    border.color: completed ? "#43876b" : "#b88632"
                    border.width: 2
                    ColumnLayout {
                        id: chargerCard
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        RowLayout {
                            Layout.fillWidth: true
                            UiLabel {
                                text: chargerTile.modelData.external_id
                                font.pixelSize: 20
                                font.bold: true
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            UiIconButton {
                                objectName: "navigateCharger_" + chargerTile.modelData.id
                                symbol: "navigate"
                                text: destinationUrl ? "Navegar no Google Maps" : "Sem coordenadas ou morada para navegar"
                                Accessible.name: text + " · " + chargerTile.modelData.external_id
                                property string destinationUrl: win.navigationUrl(chargerTile.modelData.details || {})
                                Layout.preferredWidth: win.touchHeight
                                Layout.minimumWidth: win.touchHeight
                                enabled: destinationUrl.length > 0
                                onClicked: Qt.openUrlExternally(destinationUrl)
                            }
                        }
                        UiLabel {
                            objectName: "chargerExtra_" + chargerTile.modelData.id
                            visible: !win.androidCamera && text.length > 0
                            text: [modelData.details.model || "", modelData.details.cpe ? "CPE: " + modelData.details.cpe : ""].filter(v => !!v).join(" · ")
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        UiLabel {
                            objectName: "chargerSerial_" + chargerTile.modelData.id
                            visible: win.androidCamera
                            text: "S/N: " + (modelData.details.serial || "Não indicada")
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        UiLabel {
                            objectName: "chargerAddress_" + chargerTile.modelData.id
                            text: win.androidCamera ? "Morada: " + (win.addressLine(modelData.details) || "Não indicada") : [win.addressLine(modelData.details), "S/N: " + (modelData.details.serial || "Não indicada")].filter(v => !!v).join(" · ")
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        UiLabel {
                            objectName: "chargerStatus_" + chargerTile.modelData.id
                            text: chargerTile.completed ? "Concluído" : "Por fazer"
                            font.bold: true
                            color: chargerTile.completed ? win.success : "#775212"
                        }
                        UiLabel {
                            visible: !win.androidCamera
                            text: modelData.reports.length ? modelData.reports.length + " report(s) existente(s)" : "Sem report"
                            color: modelData.reports.length ? win.brandPrimary : "#775e2d"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            wrapMode: Text.Wrap
                        }
                        Flow {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 8
                            UiButton {
                                primary: true
                                implicitHeight: win.touchHeight
                                text: "Nova intervenção"
                                enabled: !lem.busy
                                onClicked: {
                                    win.openCatalogReport(modelData.id, "");
                                }
                            }
                            Repeater {
                                model: modelData.reports
                                UiButton {
                                    id: catalogReportButton
                                    required property var modelData
                                    readonly property string interventionDate: {
                                        const value = modelData.status === "closed" ? modelData.finished : modelData.started;
                                        if (!value) return "";
                                        const date = new Date(value);
                                        if (isNaN(date.getTime())) return "";
                                        return modelData.status === "closed" ? Qt.formatDateTime(date, "dd_MM_HH")
                                                                           : "Início " + Qt.formatDateTime(date, "dd/MM/yyyy HH:mm:ss");
                                    }
                                    // Keep the full reference visible on narrow catalogue cards.
                                    wrapText: true
                                    implicitHeight: Math.max(win.touchHeight, implicitContentHeight + topPadding + bottomPadding)
                                    objectName: "catalogReport_" + modelData.id
                                    width: Math.min(implicitWidth, parent.width)
                                    text: (modelData.status === "closed" ? ["Fechado", interventionDate].filter(Boolean).join(" ") : "Rascunho")
                                          + " • " + (modelData.identifier || "") + (modelData.dirty ? " · por enviar" : "")
                                          + (modelData.status !== "closed" && interventionDate ? "\n" + interventionDate : "")
                                    ToolTip.visible: hovered
                                    ToolTip.text: catalogReportButton.text
                                    enabled: !lem.busy
                                    onClicked: {
                                        win.openCatalogReport("", modelData.id);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        Item {
            id: pcvePage
            visible: win.page === "pcves"
            anchors.fill: parent
            readonly property string userId: lem.user.id || ""
            readonly property bool signedIn: !!lem.authenticated
            function refreshOnEntry() {
                if (visible && signedIn)
                    lem.refreshStations();
            }
            onVisibleChanged: refreshOnEntry()
            onSignedInChanged: refreshOnEntry()
            onUserIdChanged: {
                win.stationReportOrigin = null;
                pcveSearch.clear();
                pcveDetailsDialog.close();
                if (!userId && win.page === "pcves")
                    win.page = "catalog";
            }
            ChromeBackdrop {}
            ScrollChrome {
                id: pcveChrome
                contentSpacing: 5
                RowLayout {
                    Layout.fillWidth: true
                    UiLabel {
                        text: "PCVEs"
                        font.pixelSize: 23
                        font.bold: true
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                    UiButton {
                        objectName: "refreshPcveCatalogue"
                        text: "Atualizar"
                        enabled: pcvePage.signedIn && !lem.stationCatalogueLoading
                        onClicked: lem.refreshStations()
                    }
                }
                UiButton {
                    objectName: "importPcves"
                    text: "Importar Excel"
                    visible: win.canManagePcves
                    enabled: !lem.busy
                    Layout.fillWidth: true
                    onClicked: pcveImportDialog.start("")
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    UiTextField {
                        id: pcveSearch
                        objectName: "pcveSearch"
                        property string needle: ""
                        placeholderText: "Pesquisar IDs ou dados do PCVE…"
                        Accessible.name: "Pesquisar por ID, série, morada, modelo ou CPE"
                        Layout.fillWidth: true
                        selectByMouse: true
                        Keys.onEscapePressed: clear()
                        onAccepted: Qt.inputMethod.hide()
                        onTextChanged: {
                            if (text.trim().length) {
                                pcveSearchDelay.restart();
                            } else {
                                pcveSearchDelay.stop();
                                needle = "";
                            }
                        }
                        Timer {
                            id: pcveSearchDelay
                            interval: 180
                            onTriggered: pcveSearch.needle = pcveSearch.text.trim()
                        }
                    }
                    UiIconButton {
                        objectName: "clearPcveSearch"
                        symbol: "erase"
                        text: "Limpar pesquisa"
                        visible: pcveSearch.text.length > 0
                        onClicked: {
                            pcveSearch.clear();
                            pcveSearch.forceActiveFocus();
                        }
                    }
                }
                UiLabel {
                    objectName: "pcveCatalogueStatus"
                    visible: text.length > 0
                    text: {
                        if (lem.stationCatalogueMessage)
                            return lem.stationCatalogueMessage;
                        if (lem.stationCatalogueLoading && !lem.stationCatalogueLoaded)
                            return "A carregar os IDs de PCVEs…";
                        if (pcveList.count > 0)
                            return "";
                        if (win.pcveRecords.length)
                            return "Nenhum PCVE corresponde à pesquisa.";
                        if (!pcvePage.signedIn && !lem.stationCatalogueLoaded)
                            return "Inicie sessão para carregar a base de IDs de PCVEs.";
                        return "Sem IDs de PCVEs disponíveis.";
                    }
                    color: win.mutedInk
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
            ListView {
                id: pcveList
                objectName: "pcveList"
                anchors.fill: parent
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                boundsMovement: Flickable.StopAtBounds
                spacing: 8
                reuseItems: true
                model: pcvePage.visible ? win.pcveRecords.filter(c => win.chargerMatches(c, pcveSearch.needle)) : []
                header: Item {
                    width: pcveList.width
                    height: win.pageTopInset
                }
                footer: Item {
                    width: pcveList.width
                    height: win.pageBottomInset
                }
                ScrollBar.vertical: UiPageScrollBar {
                    pageView: pcveList
                }
                delegate: UiButton {
                    id: pcveCard
                    required property var modelData
                    readonly property var details: modelData.details || {}
                    objectName: "pcveCard_" + modelData.id
                    text: modelData.external_id
                    Accessible.description: "Ver dados do PCVE"
                    width: pcveList.width
                    height: Math.max(win.touchHeight, pcveSummary.implicitHeight + topPadding + bottomPadding)
                    contentItem: RowLayout {
                        id: pcveSummary
                        spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 3
                            UiLabel {
                                text: pcveCard.text
                                textFormat: Text.PlainText
                                font.pixelSize: 17
                                font.bold: true
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                            }
                            UiLabel {
                                text: [pcveCard.details.manufacturer, pcveCard.details.model, win.addressLine(pcveCard.details)]
                                    .filter(value => !!value).join(" · ")
                                textFormat: Text.PlainText
                                visible: text.length > 0
                                color: win.mutedInk
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                        UiIcon {
                            kind: "down"
                            rotation: -90
                            ink: win.brandPrimary
                        }
                    }
                    onClicked: {
                        Qt.inputMethod.hide();
                        pcveDetailsDialog.stationId = modelData.id;
                        pcveDetailsDialog.open();
                    }
                }
            }
        }
        Item {
            id: reportPage
            visible: win.page === "report"
            anchors.fill: parent
            // Consume the Android key press and release on the same page. Changing
            // focus on press can let an unhandled release background the activity.
            readonly property bool handlesSystemBack: win.androidCamera && reportBackShortcut.enabled
            Keys.onShortcutOverride: event => {
                if (event.key === Qt.Key_Back && handlesSystemBack)
                    event.accepted = true;
            }
            Keys.onBackPressed: event => { event.accepted = handlesSystemBack; }
            Keys.onReleased: event => {
                if (event.key === Qt.Key_Back && handlesSystemBack) {
                    event.accepted = true;
                    if (!event.isAutoRepeat)
                        win.navigateBack();
                }
            }
            ChromeBackdrop {}
            ScrollChrome {
                id: reportChrome
                objectName: "reportChrome"
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    UiLabel {
                        text: win.reportData.snapshot ? win.reportData.snapshot.charger.external_id : "A abrir report…"
                        font.pixelSize: win.compact ? 18 : 24
                        font.bold: true
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    UiLabel {
                        objectName: "reportIdentifier"
                        text: win.reportData.identifier || ""
                        visible: text.length > 0
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                    UiLabel {
                        objectName: "reportSaveState"
                        text: (win.reportData.status === "closed" ? "Concluído" : "Rascunho") + " • " + (lem.localSaveError ? "Erro ao guardar · mantenha a app aberta" : lem.saving ? "A guardar no dispositivo…" : win.reportData._contract_unavailable ? "Sincronização suspensa · contrato em eliminação" : win.reportData.dirty ? "Guardado no dispositivo · por sincronizar" : "Sincronizado") + " • " + win.totalAnswered() + "/" + win.totalItems() + " respostas"
                        font.pixelSize: 14
                        color: win.mutedInk
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    UiLabel {
                        objectName: "correctionNotice"
                        visible: win.correctingReport
                        text: win.reportData.status === "closed" ? "Correção por confirmar no servidor. O original continua disponível até à substituição." : "Cópia de correção: pode editar respostas, observações e evidências. Para alterar as datas, use «Editar início e fim» no menu do report. O original mantém-se disponível."
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: win.mutedInk
                    }
                    UiLabel {
                        objectName: "contractSyncNotice"
                        visible: !!win.reportData._contract_unavailable
                        text: win.reportData._contract_unavailable || ""
                        wrapMode: Text.Wrap
                        color: "#9a5900"
                        Layout.fillWidth: true
                    }
                }
                Rectangle {
                    objectName: "reportCompletionMeter"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 3
                    radius: 1.5
                    color: win.brandTrack
                    Accessible.role: Accessible.ProgressBar
                    Accessible.name: "Respostas preenchidas: " + win.totalAnswered() + " de " + win.totalItems()
                    Rectangle {
                        width: parent.width * (win.totalItems() ? win.totalAnswered() / win.totalItems() : 0)
                        height: parent.height
                        radius: parent.radius
                        color: win.brandPrimary
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    UiButton {
                        visible: !win.compact
                        symbol: "left"
                        implicitWidth: win.touchHeight
                        Accessible.name: "Grupo anterior"
                        enabled: win.selectedSection > 0
                        onClicked: win.selectSection(win.selectedSection - 1)
                    }
                    Flickable {
                        id: sectionTabScroll
                        onWidthChanged: Qt.callLater(sectionTabs.revealCurrent)
                        implicitWidth: 0
                        Layout.minimumWidth: 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: win.compact ? 56 : 64
                        clip: true
                        contentWidth: sectionTabs.width
                        contentHeight: height
                        flickableDirection: Flickable.HorizontalFlick
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.horizontal: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }
                        Row {
                            id: sectionTabs
                            objectName: "reportSectionTabs"
                            width: Math.max(sectionTabScroll.width, lem.sections.length * 88)
                            height: 56
                            spacing: 4
                            property int currentIndex: win.selectedSection
                            function itemAt(index) {
                                return sectionRepeater.itemAt(index);
                            }
                            function revealCurrent() {
                                const item = itemAt(currentIndex);
                                if (!item || sectionTabScroll.width <= 0)
                                    return;
                                const view = sectionTabScroll;
                                if (item.x < view.contentX)
                                    view.contentX = item.x;
                                else if (item.x + item.width > view.contentX + sectionTabScroll.width)
                                    view.contentX = item.x + item.width - sectionTabScroll.width;
                            }
                            onCurrentIndexChanged: Qt.callLater(revealCurrent)
                            onWidthChanged: Qt.callLater(revealCurrent)
                            Repeater {
                                id: sectionRepeater
                                model: lem.sections
                                Base.TabButton {
                                    id: sectionTab
                                    required property var modelData
                                    required property int index
                                    checked: win.selectedSection === index
                                    autoExclusive: false
                                    onClicked: win.selectSection(index)
                                    Keys.onLeftPressed: {
                                        win.selectSection(Math.max(0, index - 1));
                                        sectionTabs.itemAt(win.selectedSection).forceActiveFocus(Qt.TabFocusReason);
                                    }
                                    Keys.onRightPressed: {
                                        win.selectSection(Math.min(lem.sections.length - 1, index + 1));
                                        sectionTabs.itemAt(win.selectedSection).forceActiveFocus(Qt.TabFocusReason);
                                    }
                                    objectName: "sectionTab_" + modelData.id
                                    width: Math.max(84, (sectionTabs.width - 28) / Math.max(1, lem.sections.length))
                                    height: 56
                                    y: 0
                                    text: modelData.id === "SEG" && !win.compact ? "Segurança" : modelData.id
                                    Accessible.name: modelData.title + ", " + win.groupMissing(modelData) + " itens por validar"
                                    padding: 8
                                    contentItem: Column {
                                        spacing: 2
                                        Text {
                                            width: parent.width
                                            text: sectionTab.text
                                            elide: Text.ElideRight
                                            font.bold: true
                                            font.pixelSize: 16
                                            horizontalAlignment: Text.AlignHCenter
                                            color: sectionTab.checked ? "#ffffff" : win.ink
                                        }
                                        Text {
                                            width: parent.width
                                            text: win.sectionAnswered(sectionTab.modelData) + "/" + sectionTab.modelData.items.length
                                            font.pixelSize: 14
                                            horizontalAlignment: Text.AlignHCenter
                                            color: sectionTab.checked ? "#ffffff" : win.mutedInk
                                        }
                                    }
                                    background: Rectangle {
                                        radius: 8
                                        color: sectionTab.checked ? win.brandPrimary : sectionTab.hovered ? win.hoverSurface : "#ffffff"
                                        border.color: win.outline
                                        border.width: sectionTab.visualFocus ? 3 : 1
                                        Rectangle {
                                            anchors.bottom: parent.bottom
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: parent.width - 16
                                            height: 3
                                            color: sectionTab.checked ? "#ffffff" : "transparent"
                                        }
                                    }
                                    ToolTip.visible: hovered
                                    ToolTip.text: modelData.title
                                }
                            }
                        }
                    }
                    UiButton {
                        visible: win.compact
                        objectName: "allGroupsButton"
                        symbol: "menu"
                        implicitWidth: win.touchHeight
                        Accessible.name: "Ver todos os grupos"
                        onClicked: groupDialog.open()
                    }
                    UiButton {
                        visible: !win.compact
                        symbol: "right"
                        objectName: "nextSectionButton"
                        implicitWidth: win.touchHeight
                        Accessible.name: "Grupo seguinte"
                        enabled: win.selectedSection < lem.sections.length - 1
                        onClicked: win.selectSection(win.selectedSection + 1)
                    }
                }
            }
            Item {
                anchors.fill: parent
                z: 5
                enabled: win.androidCamera
                PointHandler {
                    acceptedDevices: PointerDevice.TouchScreen | PointerDevice.TouchPad
                    property real dx: 0
                    property real dy: 0
                    property int startSection: -1
                    property string startReport: ""
                    objectName: "groupSwipe"
                    onPointChanged: if (active) {
                        dx = point.scenePosition.x - point.scenePressPosition.x;
                        dy = point.scenePosition.y - point.scenePressPosition.y;
                    }
                    onActiveChanged: {
                        if (active) {
                            dx = 0;
                            dy = 0;
                            startSection = win.selectedSection;
                            startReport = lem.current.id || "";
                        } else if (startSection === win.selectedSection && startReport === lem.current.id && Math.abs(dx) >= 64 && Math.abs(dx) > Math.abs(dy) * 1.6 && !Qt.inputMethod.visible) {
                            reportScroll.cancelFlick();
                            win.selectSection(startSection + (dx < 0 ? 1 : -1));
                        }
                    }
                }
            }
            Flickable {
                id: reportScroll
                activeFocusOnTab: true
                Keys.onPressed: event => win.scrollPageKey(reportScroll, event)
                objectName: "reportScroll"
                anchors.fill: parent
                contentWidth: width
                // Bound to the actual final question, not a container's inferred extent.
                contentHeight: {
                    const panel = sectionPanels.count > win.selectedSection ? sectionPanels.itemAt(win.selectedSection) : null;
                    return panel && panel.lastCheckpointBottom > 0 ? panel.y + panel.lastCheckpointBottom + win.pageBottomInset : reportBody.implicitHeight;
                }
                boundsBehavior: Flickable.StopAtBounds
                boundsMovement: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                clip: true
                ScrollBar.vertical: UiPageScrollBar {
                    pageView: reportScroll
                }
                ColumnLayout {
                    id: reportBody
                    width: reportScroll.width
                    height: implicitHeight
                    spacing: 12
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(0, win.pageTopInset - parent.spacing)
                    }
                    UiLabel {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: "Proprietário: " + (win.reportData.technician || (win.reportData.owner_id === lem.user.id ? lem.user.name : win.reportData.owner_id || "—")) + (win.editing ? " · pode editar" : " · só leitura")
                        font.pixelSize: 14
                    }
                    Rectangle {
                        visible: !!win.reportData._conflict
                        Layout.fillWidth: true
                        implicitHeight: conflictBody.implicitHeight + 24
                        radius: 8
                        color: "#fff0ce"
                        border.color: "#9a5900"
                        ColumnLayout {
                            id: conflictBody
                            anchors.fill: parent
                            anchors.margins: 12
                            UiLabel {
                                Layout.fillWidth: true
                                text: "Alterações locais protegidas"
                                font.bold: true
                                color: "#713d00"
                            }
                            UiLabel {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: (win.reportData._conflict_reason || "A propriedade ou a revisão mudou no servidor.") + " As respostas e fotografias locais foram conservadas. Pode criar outro rascunho a partir desta cópia."
                            }
                            UiButton {
                                objectName: "viewConflictServer"
                                Layout.fillWidth: true
                                text: "Consultar versão do servidor"
                                enabled: !win.reportNetworkActivity
                                onClicked: {
                                    lem.viewServerReport();
                                    serverVersionDialog.open();
                                }
                            }
                            UiButton {
                                objectName: "openCurrentServerReport"
                                Layout.fillWidth: true
                                text: "Abrir versão atual após criar cópia"
                                visible: !!win.reportData._conflict_copy_id
                                enabled: !win.reportNetworkActivity
                                onClicked: lem.useServerReport()
                            }
                            UiButton {
                                objectName: "copyConflictReport"
                                Layout.fillWidth: true
                                text: "Criar novo rascunho desta cópia"
                                primary: true
                                enabled: !win.reportNetworkActivity
                                onClicked: copyReportDialog.open()
                            }
                        }
                    }
                    UiButton {
                        objectName: "downloadAllReportPhotosButton"
                        Layout.fillWidth: true
                        visible: win.reportData.revision > 0 || (win.reportData.photos || []).some(p => p.uploaded)

                        text: lem.downloadingPhotos ? "A descarregar evidências…" : "Descarregar evidências"
                        Accessible.name: "Descarregar todas as evidências deste report do servidor"
                        enabled: !lem.busy && !lem.downloadingPhotos
                        onClicked: win.openPhotoDownloads()
                    }
                    Repeater {
                        id: sectionPanels
                        model: lem.sections
                        ColumnLayout {
                            id: sectionCard
                            property var checkpoints: sectionLoader.item ? sectionLoader.item.checkpoints : null
                            readonly property real lastCheckpointBottom: {
                                const last = checkpoints && checkpoints.count ? checkpoints.itemAt(checkpoints.count - 1) : null;
                                return last && sectionLoader.item ? sectionLoader.y + sectionLoader.item.y + last.y + last.height : 0;
                            }
                            required property var modelData
                            required property int index
                            objectName: "sectionPanel_" + modelData.id
                            visible: index === win.selectedSection
                            Layout.fillWidth: true
                            spacing: 8
                            Loader {
                                // Instantiate only the selected question group. Avoid all-checklist delegate rebuilds per tap.
                                id: sectionLoader
                                Layout.fillWidth: true
                                active: sectionCard.visible
                                sourceComponent: ColumnLayout {
                                    property alias checkpoints: checkpointRepeater
                                    width: sectionLoader.width
                                    spacing: 8
                                    UiLabel {
                                        text: sectionCard.modelData.title
                                        font.pixelSize: 19
                                        font.bold: true
                                        color: win.brandPrimary
                                        wrapMode: Text.Wrap
                                        Layout.fillWidth: true
                                        Layout.topMargin: 4
                                    }
                                    UiButton {
                                        objectName: "copyQveGroup"
                                        text: "Copiar QVE de outro carregador…"
                                        visible: win.editing && sectionCard.modelData.id === "QVE"
                                        enabled: !lem.busy && !lem.syncing && !lem.attachingFiles
                                        Layout.fillWidth: true
                                        onClicked: { qveCopyDialog.scope = "QVE"; qveCopyDialog.open(); }
                                    }
                                    UiLabel {
                                        visible: sectionCard.modelData.id === "QVE" && !!((lem.current.payload || {}).qve_copy)
                                        text: "Origem QVE: " + (((lem.current.payload || {}).qve_copy || {}).source_station_id || "") + " · " + (((lem.current.payload || {}).qve_copy || {}).copied_at || "")
                                        Layout.fillWidth: true
                                        wrapMode: Text.Wrap
                                    }
                                    UiButton {
                                        objectName: "copyMeasurementsGroup"
                                        text: "Copiar Medições de outro carregador…"
                                        visible: win.editing && sectionCard.modelData.id === "MAN"
                                        enabled: !lem.busy && !lem.syncing && !lem.attachingFiles
                                        Layout.fillWidth: true
                                        onClicked: { qveCopyDialog.scope = "MAN"; qveCopyDialog.open(); }
                                    }
                                    UiLabel {
                                        visible: !!lem.copyingGroup && sectionCard.modelData.id === lem.copyingGroup
                                        text: "A copiar respostas e evidências… Pode continuar a editar os restantes campos."
                                        Layout.fillWidth: true
                                        wrapMode: Text.Wrap
                                    }
                                    Repeater {
                                        id: checkpointRepeater
                                        model: sectionCard.modelData.items
                                        Rectangle {
                                            id: itemCard
                                            required property var modelData
                                            property bool noteExpanded: false
                                            property bool notesRequired: !modelData.notes_disabled && (win.answerFor(modelData.id).assessment === "NOK" || answerValue === "NOK" || (!!modelData.na_notes_required && Measurements.notApplicable(answerValue)))
                                            enabled: !(lem.copyingGroup === "QVE" && modelData.id.indexOf("QVE.") === 0) && !(lem.copyingGroup === "MAN" && /^MAN\.(0[4-9]|10)$/.test(modelData.id))
                                            property string answerValue: win.answerFor(modelData.id).value || ""
                                            readonly property string measurementId: Number((lem.current.template || {}).version || 7) < 7 && Measurements.milliamp(modelData.id) ? Measurements.canonical(modelData.id) : modelData.id
                                            property bool needsPhoto: modelData.photo_required && (modelData.photo_always || !Measurements.notApplicable(answerValue)) && win.photographsFor(modelData.id).length === 0
                                            objectName: "checkpoint_" + modelData.id
                                            Layout.fillWidth: true
                                            implicitHeight: itemContent.implicitHeight + 24
                                            color: "white"
                                            radius: 12
                                            border.color: win.focusedCheckpoint === modelData.id ? win.brandPrimary : win.cardOutline
                                            border.width: win.focusedCheckpoint === modelData.id ? 2 : 1
                                            ColumnLayout {
                                                id: itemContent
                                                anchors.left: parent.left
                                                anchors.right: parent.right
                                                anchors.top: parent.top
                                                anchors.margins: 12
                                                spacing: 8
                                                UiLabel {
                                                    text: itemCard.modelData.id
                                                    color: win.mutedInk
                                                    font.pixelSize: 14
                                                    Layout.fillWidth: true
                                                }
                                                UiLabel {
                                                    text: itemCard.modelData.label
                                                    wrapMode: Text.Wrap
                                                    Layout.fillWidth: true
                                                    font.bold: true
                                                }
                                                UiLabel {
                                                    objectName: "measurementHelp_" + itemCard.modelData.id
                                                    // Frozen/offline templates can retain the retired MAN.18 hint.
                                                    visible: text.length > 0
                                                    text: itemCard.modelData.id === "MAN.18" ? "" : itemCard.modelData.help || ""
                                                    wrapMode: Text.Wrap
                                                    Layout.fillWidth: true
                                                    color: win.mutedInk
                                                }
                                                UiLabel {
                                                    objectName: "previousValue_" + itemCard.modelData.id
                                                    visible: !!win.answerFor(itemCard.modelData.id).previous_value
                                                    text: "Valor anterior: " + (win.answerFor(itemCard.modelData.id).previous_value || "") + ". Confirme e introduza o valor em mA."
                                                    wrapMode: Text.Wrap
                                                    Layout.fillWidth: true
                                                    color: win.danger
                                                }
                                                RowLayout {
                                                    visible: win.editing && (itemCard.modelData.kind === "status" || !!itemCard.modelData.allow_status)
                                                    Layout.fillWidth: true
                                                    spacing: 4
                                                    Repeater {
                                                        model: ["OK", "NOK", "NA"]
                                                        UiButton {
                                                            required property string modelData
                                                            objectName: "answer_" + itemCard.modelData.id + "_" + modelData
                                                            Layout.fillWidth: true
                                                            Layout.minimumWidth: 0
                                                            implicitWidth: 0
                                                            padding: 8
                                                            text: modelData === "NA" ? "N/A" : modelData
                                                            checked: itemCard.modelData.reading_required && modelData !== "NA" ? win.answerFor(itemCard.modelData.id).assessment === modelData : modelData === "NA" ? Measurements.notApplicable(itemCard.answerValue) : itemCard.answerValue === modelData
                                                            selectedColor: modelData === "NOK" ? win.danger : modelData === "OK" ? win.success : win.neutralAnswer
                                                            font.bold: checked
                                                            enabled: !lem.busy
                                                            Accessible.name: itemCard.modelData.id + ", " + text
                                                            onClicked: {
                                                                if (itemCard.modelData.reading_required && modelData !== "NA")
                                                                    lem.setAssessment(itemCard.modelData.id, modelData);
                                                                else lem.answer(itemCard.modelData.id, modelData, notes.text);
                                                            }
                                                        }
                                                    }
                                                    UiIconButton {
                                                        objectName: "clearAnswer_" + itemCard.modelData.id
                                                        symbol: "erase"
                                                        text: "Limpar resposta"
                                                        Accessible.name: text + " do item " + itemCard.modelData.id
                                                        Layout.preferredWidth: win.touchHeight
                                                        Layout.minimumWidth: win.touchHeight
                                                        enabled: !lem.busy && !!itemCard.answerValue
                                                        opacity: enabled ? 1 : .45
                                                        onClicked: lem.answer(itemCard.modelData.id, "", notes.text)
                                                    }
                                                }

                                                UiTextField {
                                                    id: valueField
                                                    objectName: "value_" + itemCard.modelData.id
                                                    visible: win.editing && itemCard.modelData.kind === "text"
                                                    Layout.fillWidth: true
                                                    // NA is a selected state, never partially editable text.
                                                    placeholderText: Measurements.milliamp(itemCard.measurementId) ? "Valor em mA (até 6 algarismos)" : itemCard.modelData.id === "FIN.02" ? "Nome" : /^MAN\.(14|15|16|17)$/.test(itemCard.modelData.id) ? "Valor medido (ou NA)" : itemCard.modelData.reading_required ? (Measurements.unit(itemCard.measurementId) === "Ω" ? "Valor medido em Ω (ou NA)" : itemCard.modelData.id === "MAN.12" ? "Intensidade de disparo medida (ou NA)" : "Valor medido em MΩ (ou NA)") : itemCard.modelData.allow_status && (itemCard.answerValue === "OK" || (itemCard.answerValue === "NOK" || win.answerFor(itemCard.modelData.id).assessment === "NOK")) ? itemCard.answerValue + " selecionado" : Measurements.notApplicable(itemCard.answerValue) && Measurements.digits(itemCard.measurementId) ? "NA" : Measurements.digits(itemCard.measurementId) ? (Measurements.unit(itemCard.measurementId) === "V" ? "Ex.: 238,3" : "Ex.: 4564,4") : "Valor / resultado (ou NA)"
                                                    rightPadding: Measurements.digits(itemCard.measurementId) ? 38 : 12
                                                    UiLabel {
                                                        anchors.right: parent.right
                                                        anchors.rightMargin: 12
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        text: Measurements.unit(itemCard.measurementId)
                                                        color: win.mutedInk
                                                        visible: Measurements.digits(itemCard.measurementId) > 0 && !Measurements.notApplicable(itemCard.answerValue) && itemCard.answerValue !== "OK" && itemCard.answerValue !== "NOK"
                                                    }
                                                    validator: RegularExpressionValidator {
                                                        regularExpression: Measurements.editorPattern(itemCard.measurementId)
                                                    }
                                                    inputMethodHints: Measurements.milliamp(itemCard.measurementId) ? Qt.ImhDigitsOnly : Measurements.digits(itemCard.measurementId) ? Qt.ImhFormattedNumbersOnly : Qt.ImhNone
                                                    text: itemCard.modelData.id === "MAN.18" && Measurements.notApplicable(itemCard.answerValue) ? "NA" : Measurements.editorText(itemCard.measurementId, win.answerFor(itemCard.modelData.id).value)
                                                    enabled: !lem.busy
                                                    selectByMouse: true
                                                    Accessible.name: itemCard.modelData.label
                                                    onTextEdited: lem.answer(itemCard.modelData.id, text, notes.text)
                                                    onEditingFinished: {
                                                        if (Measurements.digits(itemCard.measurementId)) {
                                                            const formatted = Measurements.format(itemCard.measurementId, text);
                                                            if (formatted !== null && formatted !== text)
                                                                lem.answer(itemCard.modelData.id, formatted, notes.text);
                                                        }
                                                    }
                                                }
                                                UiButton {
                                                    objectName: "sign_" + itemCard.modelData.id
                                                    visible: win.editing && !!itemCard.modelData.supports_signature
                                                    text: (((lem.current.payload || {}).signatures || {})[itemCard.modelData.id]) ? "Assinar novamente" : "Assinar"
                                                    enabled: !lem.busy && itemCard.answerValue.trim().length > 0
                                                    Layout.fillWidth: true
                                                    onClicked: {
                                                        valueField.focus = false;
                                                        signatureDialog.checkpoint = itemCard.modelData.id;
                                                        signatureDialog.signerName = itemCard.answerValue;
                                                        signatureDialog.open();
                                                    }
                                                }
                                                UiButton {
                                                    objectName: "importSignature_" + itemCard.modelData.id
                                                    visible: win.editing && !win.androidCamera && !!itemCard.modelData.supports_signature
                                                    text: "Anexar fotografia da assinatura"
                                                    Layout.fillWidth: true
                                                    enabled: !lem.busy && itemCard.answerValue.trim().length > 0
                                                    onClicked: {
                                                        valueField.focus = false;
                                                        signaturePhotoDialog.checkpoint = itemCard.modelData.id;
                                                        signaturePhotoDialog.open();
                                                    }
                                                }
                                                UiLabel {
                                                    visible: !!itemCard.modelData.supports_signature
                                                    text: {
                                                        const signature = (((lem.current.payload || {}).signatures || {})[itemCard.modelData.id]);
                                                        return signature ? "Assinado por " + signature.signed_by + " · " + signature.signed_at : "Assinatura ainda não recolhida";
                                                    }
                                                    Layout.fillWidth: true
                                                    wrapMode: Text.Wrap
                                                }
                                                RowLayout {
                                                    visible: win.editing && Measurements.digits(itemCard.measurementId) > 0 && !itemCard.modelData.allow_status
                                                    Layout.fillWidth: true
                                                    UiButton {
                                                        objectName: "notApplicable_" + itemCard.modelData.id
                                                        text: "NA"
                                                        Accessible.name: "Não Aplicável"
                                                        checked: Measurements.notApplicable(itemCard.answerValue)
                                                        enabled: !lem.busy
                                                        onClicked: lem.answer(itemCard.modelData.id, "Não Aplicável", notes.text)
                                                    }
                                                    UiIconButton {
                                                        objectName: "clearMeasurement_" + itemCard.modelData.id
                                                        symbol: "erase"
                                                        text: "Limpar resposta"
                                                        Accessible.name: text + " do item " + itemCard.modelData.id
                                                        Layout.preferredWidth: win.touchHeight
                                                        enabled: !lem.busy && !!itemCard.answerValue
                                                        opacity: enabled ? 1 : .45
                                                        onClicked: lem.answer(itemCard.modelData.id, "", notes.text)
                                                    }
                                                    Item {
                                                        Layout.fillWidth: true
                                                    }
                                                }
                                                UiLabel {
                                                    objectName: "measurementNok_" + itemCard.modelData.id
                                                    visible: Measurements.outside(itemCard.modelData.id, itemCard.answerValue)
                                                    text: "NOK — fora dos limites"
                                                    color: win.danger
                                                    font.bold: true
                                                    wrapMode: Text.Wrap
                                                    Layout.fillWidth: true
                                                }
                                                UiLabel {
                                                    visible: !win.editing
                                                    text: "Resposta: " + (Measurements.format(itemCard.measurementId, itemCard.answerValue) || itemCard.answerValue || (itemCard.modelData.optional ? "Opcional" : "Por preencher")) + (win.answerFor(itemCard.modelData.id).assessment ? " · " + win.answerFor(itemCard.modelData.id).assessment : "")
                                                    font.bold: true
                                                    color: (itemCard.answerValue === "NOK" || win.answerFor(itemCard.modelData.id).assessment === "NOK") ? win.danger : itemCard.answerValue === "OK" ? win.success : win.mutedInk
                                                    wrapMode: Text.Wrap
                                                    Layout.fillWidth: true
                                                }
                                                UiLabel {
                                                    visible: win.editing && itemCard.notesRequired
                                                    text: (itemCard.answerValue === "NOK" || win.answerFor(itemCard.modelData.id).assessment === "NOK") ? "Observação obrigatória" : "Observação obrigatória em N/A"
                                                    color: win.danger
                                                    Layout.fillWidth: true
                                                }
                                                UiTextArea {
                                                    id: notes
                                                    objectName: "notes_" + itemCard.modelData.id
                                                    Layout.fillWidth: true
                                                    placeholderText: (itemCard.answerValue === "NOK" || win.answerFor(itemCard.modelData.id).assessment === "NOK") ? "Descreva a não conformidade" : itemCard.notesRequired ? "Indique o motivo de N/A" : "Observações"
                                                    wrapMode: TextEdit.Wrap
                                                    visible: win.editing && !itemCard.modelData.notes_disabled && (itemCard.modelData.kind === "notes" || itemCard.modelData.notes_visible || itemCard.noteExpanded || itemCard.notesRequired || text.length > 0)
                                                    text: win.notesForDisplay(itemCard.modelData.id)
                                                    enabled: !lem.busy
                                                    selectByMouse: true
                                                    Accessible.name: itemCard.modelData.id + ", observações"
                                                    onTextChanged: if (activeFocus)
                                                        lem.answer(itemCard.modelData.id, win.answerFor(itemCard.modelData.id).value || "", text)
                                                    onActiveFocusChanged: if (activeFocus) {
                                                        // Keep the editor open before clearing generated text. Optional
                                                        // observations (e.g. MAN.18 OK) would otherwise hide and lose focus.
                                                        itemCard.noteExpanded = true;
                                                        lem.beginNotesEdit(itemCard.modelData.id);
                                                        Qt.callLater(function () {
                                                            const y = notes.mapToItem(reportBody, 0, 0).y;
                                                            if (y + notes.height > reportScroll.contentY + reportScroll.height)
                                                                reportScroll.contentY = Math.max(0, y - 24);
                                                        })
                                                    }
                                                }
                                                UiLabel {
                                                    visible: !win.editing && !!win.answerFor(itemCard.modelData.id).notes
                                                    text: win.notesForDisplay(itemCard.modelData.id)
                                                    wrapMode: Text.Wrap
                                                    Layout.fillWidth: true
                                                }
                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 8
                                                    visible: win.editing && !itemCard.modelData.media_disabled
                                                    UiLabel {
                                                        objectName: "photoRequirement_" + itemCard.modelData.id
                                                        text: itemCard.needsPhoto ? "Fotografia obrigatória em falta" : win.photosFor(itemCard.modelData.id).length ? win.photosFor(itemCard.modelData.id).length + " evidência(s) associada(s)" : itemCard.modelData.photo_required && !itemCard.modelData.photo_always && Measurements.notApplicable(itemCard.answerValue) ? "Fotografia dispensada (N/A)" : "Fotografia opcional"
                                                        wrapMode: Text.Wrap
                                                        Layout.fillWidth: true
                                                        color: itemCard.needsPhoto ? "#8a5200" : win.mutedInk
                                                        font.bold: itemCard.needsPhoto
                                                    }
                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 4
                                                        UiButton {
                                                            objectName: "addPhoto_" + itemCard.modelData.id
                                                            Layout.minimumWidth: 44
                                                            Layout.preferredWidth: win.touchHeight
                                                            Layout.maximumWidth: win.touchHeight
                                                            implicitHeight: win.touchHeight
                                                            symbol: win.androidCamera ? "camera" : "gallery"
                                                            text: win.androidCamera ? "Tirar fotografia" : "Adicionar fotografia"
                                                            Accessible.name: text + " · " + itemCard.modelData.id
                                                            ToolTip.visible: hovered
                                                            ToolTip.text: text
                                                            enabled: win.editing && !lem.busy
                                                            onClicked: {
                                                                if (win.androidCamera)
                                                                    lem.takePhoto(itemCard.modelData.id);
                                                                else {
                                                                    win.photoItem = itemCard.modelData.id;
                                                                    photoDialog.open();
                                                                }
                                                            }
                                                        }
                                                        UiButton {
                                                            objectName: "choosePhoto_" + itemCard.modelData.id
                                                            Layout.minimumWidth: 44
                                                            Layout.preferredWidth: win.touchHeight
                                                            Layout.maximumWidth: win.touchHeight
                                                            implicitHeight: win.touchHeight
                                                            symbol: "gallery"
                                                            text: "Galeria"
                                                            visible: win.androidCamera
                                                            Accessible.name: "Escolher fotografia da galeria · " + itemCard.modelData.id
                                                            ToolTip.visible: hovered
                                                            ToolTip.text: text
                                                            enabled: win.editing && !lem.busy
                                                            onClicked: {
                                                                win.photoItem = itemCard.modelData.id;
                                                                photoDialog.open();
                                                            }
                                                        }
                                                        UiButton {
                                                            objectName: "addVideo_" + itemCard.modelData.id
                                                            Layout.minimumWidth: 44
                                                            Layout.preferredWidth: win.touchHeight
                                                            Layout.maximumWidth: win.touchHeight
                                                            implicitHeight: win.touchHeight
                                                            symbol: "video"
                                                            text: "Vídeo"
                                                            Accessible.name: "Adicionar vídeo · " + itemCard.modelData.id
                                                            ToolTip.visible: hovered
                                                            ToolTip.text: "Vídeo · até 120 segundos e 60 MiB"
                                                            enabled: win.editing && !lem.busy
                                                            onClicked: {
                                                                win.photoItem = itemCard.modelData.id;
                                                                if (win.androidCamera) lem.takeVideo(itemCard.modelData.id); else videoDialog.open();
                                                            }
                                                        }
                                                        UiButton {
                                                            objectName: "chooseVideo_" + itemCard.modelData.id
                                                            Layout.minimumWidth: 44
                                                            Layout.preferredWidth: win.touchHeight
                                                            Layout.maximumWidth: win.touchHeight
                                                            implicitHeight: win.touchHeight
                                                            symbol: "gallery"
                                                            text: "Galeria de vídeos"
                                                            visible: win.androidCamera
                                                            Accessible.name: "Escolher vídeo da galeria · " + itemCard.modelData.id
                                                            ToolTip.visible: hovered
                                                            ToolTip.text: text
                                                            enabled: win.editing && !lem.busy
                                                            UiIcon {
                                                                anchors.right: parent.right
                                                                anchors.bottom: parent.bottom
                                                                anchors.margins: 4
                                                                width: 12; height: 12
                                                                kind: "video"
                                                                ink: win.ink
                                                            }
                                                            onClicked: {
                                                                win.photoItem = itemCard.modelData.id;
                                                                videoDialog.open();
                                                            }
                                                        }
                                                        UiButton {
                                                            objectName: "toggleNotes_" + itemCard.modelData.id
                                                            visible: !itemCard.modelData.notes_button_hidden
                                                            Layout.minimumWidth: 44
                                                            Layout.preferredWidth: win.touchHeight
                                                            Layout.maximumWidth: win.touchHeight
                                                            padding: 6
                                                            text: "Obs."
                                                            checked: notes.visible
                                                            Accessible.name: (notes.visible ? "Remover observação" : "Adicionar observação") + " · " + itemCard.modelData.id
                                                            ToolTip.visible: hovered
                                                            ToolTip.text: (itemCard.answerValue === "NOK" || win.answerFor(itemCard.modelData.id).assessment === "NOK") ? "Observação obrigatória em NOK" : notes.visible ? "Remover observação" : "Adicionar observação"
                                                            enabled: !lem.busy
                                                            onClicked: {
                                                                if (notes.visible) {
                                                                    itemCard.noteExpanded = false;
                                                                    notes.focus = false;
                                                                    Qt.inputMethod.hide();
                                                                    if (notes.text.length) {
                                                                        removeNotesDialog.reportId = lem.current.id;
                                                                        removeNotesDialog.checkpoint = itemCard.modelData.id;
                                                                        removeNotesDialog.open();
                                                                    }
                                                                } else {
                                                                    itemCard.noteExpanded = true;
                                                                    notes.forceActiveFocus();
                                                                }
                                                            }
                                                        }
                                                        Item {
                                                            Layout.fillWidth: true
                                                        }
                                                    }
                                                }
                                                Flow {
                                                    Layout.fillWidth: true
                                                    spacing: 8
                                                    Repeater {
                                                        model: win.photosFor(itemCard.modelData.id)
                                                        Column {
                                                            id: photoCard
                                                            required property var modelData
                                                            property bool isVideo: (modelData.mime || "").startsWith("video/")
                                                            width: 140
                                                            spacing: 6
                                                            Image {
                                                                id: photoThumb
                                                                visible: !photoCard.isVideo && !!photoCard.modelData.path && status !== Image.Error
                                                                source: photoCard.isVideo ? "" : (photoCard.modelData.path || "")
                                                                width: 140
                                                                height: 105
                                                                fillMode: Image.PreserveAspectFit
                                                                autoTransform: true
                                                                asynchronous: true
                                                                sourceSize.width: 420
                                                                sourceSize.height: 315
                                                                MouseArea {
                                                                    anchors.fill: parent
                                                                    onClicked: {
                                                                        photoPreview.photoSource = parent.source;
                                                                        photoPreview.open();
                                                                    }
                                                                }
                                                            }
                                                            UiButton {
                                                                width: 140; height: 105
                                                                visible: photoCard.isVideo
                                                                symbol: "video"
                                                                text: "Reproduzir vídeo"
                                                                Accessible.name: text
                                                                enabled: !!photoCard.modelData.path
                                                                onClicked: lem.openVideo(photoCard.modelData.id)
                                                            }
                                                            Flow {
                                                                width: 140
                                                                spacing: 4
                                                                UiIconButton {
                                                                    width: 44; height: 44
                                                                    objectName: "enlargePhoto_" + photoCard.modelData.id
                                                                    symbol: "zoom"
                                                                    text: "Ampliar fotografia"
                                                                    visible: !photoCard.isVideo && !!photoCard.modelData.path && photoThumb.status !== Image.Error
                                                                    Accessible.name: text + " do item " + itemCard.modelData.id
                                                                    onClicked: {
                                                                        photoPreview.photoSource = photoCard.modelData.path || "";
                                                                        photoPreview.open();
                                                                    }
                                                                }
                                                                UiIconButton {
                                                                    width: 44; height: 44
                                                                    objectName: "downloadPhoto_" + photoCard.modelData.id
                                                                    symbol: "download"
                                                                    text: "Descarregar evidência"
                                                                    visible: photoCard.modelData.uploaded && (!photoCard.modelData.path || (!photoCard.isVideo && photoThumb.status === Image.Error))
                                                                    enabled: !lem.photoDownloading(photoCard.modelData.id)
                                                                    Accessible.name: text + " do item " + itemCard.modelData.id
                                                                    onClicked: lem.fetchPhoto(photoCard.modelData.id)
                                                                }
                                                                UiIconButton {
                                                                    width: 44; height: 44
                                                                    objectName: "removePhoto_" + photoCard.modelData.id
                                                                    symbol: "trash"
                                                                    text: "Remover evidência"
                                                                    destructive: true
                                                                    visible: win.editing
                                                                    enabled: !lem.busy
                                                                    Accessible.name: text + " do item " + itemCard.modelData.id
                                                                    onClicked: {
                                                                        removePhotoDialog.photoId = photoCard.modelData.id;
                                                                        removePhotoDialog.reportId = lem.current.id;
                                                                        removePhotoDialog.checkpoint = itemCard.modelData.id;
                                                                        removePhotoDialog.open();
                                                                    }
                                                                }
                                                                UiIconButton {
                                                                    width: 44; height: 44
                                                                    objectName: "editPhoto_" + photoCard.modelData.id
                                                                    symbol: "photoEdit"
                                                                    text: "Editar fotografia"
                                                                    Accessible.name: text + " do item " + itemCard.modelData.id
                                                                    visible: win.editing && !photoCard.isVideo && !!photoCard.modelData.path
                                                                    enabled: !lem.busy
                                                                    onClicked: {
                                                                        cropDialog.reportId = lem.current.id;
                                                                        cropDialog.photoId = photoCard.modelData.id;
                                                                        cropDialog.photoSource = photoCard.modelData.path;
                                                                        cropDialog.open();
                                                                    }
                                                                }
                                                            }
                                                            UiLabel {
                                                                width: 140
                                                                wrapMode: Text.Wrap
                                                                text: lem.photoDownloading(photoCard.modelData.id) ? "A descarregar…" : photoCard.modelData.uploaded ? "No servidor" : "Por enviar"
                                                                font.pixelSize: 14
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(0, win.pageBottomInset - parent.spacing)
                    }
                }
            }
        }
        Item {
            id: adminPage
            anchors.fill: parent
            visible: win.page === "admin" && lem.user.role === "admin"
            ChromeBackdrop {}
            Flickable {
                id: adminScroll
                objectName: "adminScroll"
                activeFocusOnTab: true
                Keys.onPressed: event => win.scrollPageKey(adminScroll, event)
                anchors.fill: parent
                contentWidth: width
                contentHeight: adminTail.y + adminTail.height
                boundsBehavior: Flickable.StopAtBounds
                boundsMovement: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                visible: win.page === "admin"
                clip: true
                ScrollBar.vertical: UiPageScrollBar {
                    pageView: adminScroll
                }
                ColumnLayout {
                    id: adminBody
                    width: adminScroll.width
                    height: implicitHeight
                    spacing: 12
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(0, win.pageTopInset - parent.spacing)
                    }
                    UiComboBox {
                        id: adminArea
                        objectName: "adminArea"
                        Layout.fillWidth: true
                        implicitHeight: win.touchHeight
                        model: ["Contratos", "Importar / formato dos reports", "Contas e acessos"]
                        Accessible.name: "Área de gestão"
                        onActivated: {
                            adminScroll.cancelFlick();
                            adminScroll.contentY = 0;
                            win.resetScrollChrome();
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 12
                        visible: adminArea.currentIndex === 0
                        UiLabel {
                            text: "Gestão de contratos"
                            font.pixelSize: 23
                            font.bold: true
                        }
                        Flow {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 8
                            visible: lem.authenticated && lem.user.role === "admin"
                            UiButton {
                                objectName: "manageContractIds"
                                text: "Gerir IDs do contrato"
                                enabled: !win.networkActivity && !!contractBox.selectedId
                                onClicked: {
                                    contractIdsDialog.contractId = contractBox.selectedId;
                                    contractIdsDialog.open();
                                }
                            }
                            UiButton {
                                objectName: "deleteContractButton"
                                text: "Apagar contrato selecionado"
                                enabled: !win.networkActivity && contractBox.currentIndex >= 0 && !!lem.contractId()
                                onClicked: {
                                    const contract = contractBox.model[contractBox.currentIndex];
                                    deleteContractDialog.contractId = contract.id;
                                    deleteContractDialog.contractName = contract.name;
                                    deleteContractDialog.open();
                                }
                            }
                            UiButton {
                                objectName: "deletedContractsButton"
                                text: "Recuperar contratos"
                                enabled: !win.networkActivity
                                onClicked: {
                                    lem.loadDeletedContracts();
                                    deletedContractsDialog.open();
                                }
                            }
                        }
                        UiLabel {
                            text: "Empresa"
                        }
                        UiTextField {
                            id: company
                            implicitHeight: win.touchHeight
                            placeholderText: "Empresa (Galp, Prio, Enable…)"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                        }
                        RowLayout {
                            UiLabel {
                                text: "Ano"
                            }
                            SpinBox {
                                id: year
                                objectName: "contractYear"
                                // A year is an identifier: never apply locale thousands separators.
                                textFromValue: function (value, locale) {
                                    return String(value);
                                }
                                implicitHeight: win.touchHeight
                                from: 2020
                                to: 2100
                                value: new Date().getFullYear()
                                editable: true
                            }
                            UiLabel {
                                text: "Semestre"
                            }
                            UiComboBox {
                                id: semester
                                implicitHeight: win.touchHeight
                                model: ["1.º", "2.º"]
                            }
                        }
                        UiLabel {
                            text: "Tipo de contrato"
                        }
                        UiTextField {
                            id: kind
                            implicitHeight: win.touchHeight
                            placeholderText: "Tipo de contrato"
                            text: "TRE"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                        }
                        UiButton {
                            implicitHeight: win.touchHeight
                            text: "Criar contrato"
                            primary: true
                            enabled: !lem.busy && company.text.trim().length > 0 && kind.text.trim().length > 0
                            onClicked: lem.createContract(company.text, year.value, semester.currentIndex + 1, kind.text)
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 12
                        visible: adminArea.currentIndex === 1
                        UiLabel {
                            text: "Formato dos reports e importação"
                            font.pixelSize: 19
                            font.bold: true
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                        }
                        UiLabel {
                            text: "O formato Stations usa a primeira linha como cabeçalho e importa automaticamente as colunas previstas. Confira todos os carregadores e os respetivos dados antes de confirmar. Linhas inválidas bloqueiam a importação."
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                        }
                        UiLabel {
                            text: "Escolha a entidade que emite os reports e até dois logótipos do cliente. Pode guardar estas opções sem importar um Excel. As novas exportações dos reports deste contrato usarão o formato escolhido."
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                        UiLabel { text: "Reports emitidos em nome de"; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        UiComboBox {
                            id: reportIssuer
                            objectName: "reportIssuer"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            model: ["Lem", "CSMTEC"]
                            readonly property var saveState: {
                                const state = lem.reportPresentationSave || {};
                                return state.contract_id === lem.contractId() ? state : {};
                            }
                            enabled: saveState.state !== "saving"
                            property string confirmedIssuer: "Lem"
                            property string loadedKey: ""
                            property int presentationRevision: 0
                            property string savedSelection: ""
                            readonly property bool hasUnsavedChanges: savedSelection !== "" && savedSelection !== JSON.stringify([currentText, primaryLogo.currentIndex > 0 ? primaryLogo.currentText : "", secondaryLogo.currentIndex > 0 ? secondaryLogo.currentText : ""])
                            function loadContract() {
                                const contract = lem.contracts.find(c => c.id === lem.contractId());
                                const style = contract ? (contract.presentation || {issuer: "Lem", revision: 0, client_logos: contract.report_logos || []}) : {issuer: "Lem", revision: 0, client_logos: []};
                                confirmedIssuer = style.issuer;
                                const key = (contract ? contract.id : "") + ":" + style.revision;
                                if (key === loadedKey) return;
                                loadedKey = key;
                                presentationRevision = style.revision;
                                currentIndex = style.issuer === "CSMTEC" ? 1 : 0;
                                const logos = (style.client_logos || []).filter(b => b.id !== "Lem" && b.id !== "CSMTEC");
                                primaryLogo.currentIndex = logos.length ? Math.max(0, primaryLogo.model.indexOf(logos[0].id)) : 0;
                                secondaryLogo.currentIndex = logos.length > 1 ? Math.max(0, secondaryLogo.model.indexOf(logos[1].id)) : 0;
                                savedSelection = JSON.stringify([currentText, primaryLogo.currentIndex > 0 ? primaryLogo.currentText : "", secondaryLogo.currentIndex > 0 ? secondaryLogo.currentText : ""]);
                            }
                            onActivated: lem.invalidatePreview()
                            Component.onCompleted: Qt.callLater(loadContract)
                            Connections {
                                target: lem
                                function onChanged() { reportIssuer.loadContract(); }
                            }
                        }
                        UiLabel {
                            objectName: "reportPresentationSaveStatus"
                            text: reportIssuer.saveState.state === "saved" ? "Guardado no servidor: " + reportIssuer.confirmedIssuer + "." :
                                  reportIssuer.saveState.message || "Formato guardado: " + reportIssuer.confirmedIssuer + "."
                            textFormat: Text.PlainText
                            color: reportIssuer.saveState.state === "error" ? win.danger :
                                   reportIssuer.saveState.state === "warning" ? "#9a5900" : win.ink
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                        UiLabel { text: "Logótipo do cliente principal (opcional)"; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        UiComboBox {
                            id: primaryLogo
                            objectName: "primaryReportLogo"
                            enabled: reportIssuer.saveState.state !== "saving"
                            Layout.fillWidth: true
                            model: ["Nenhum"].concat(win.reportBrandCatalogue.filter(b => b.id !== "Lem" && b.id !== "CSMTEC").map(b => b.id))
                            currentIndex: 0
                            onActivated: lem.invalidatePreview()
                        }
                        UiLabel { text: "Logótipo do cliente secundário (opcional)"; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        UiComboBox {
                            id: secondaryLogo
                            objectName: "secondaryReportLogo"
                            Layout.fillWidth: true
                            enabled: primaryLogo.currentIndex > 0 && reportIssuer.saveState.state !== "saving"
                            model: ["Nenhum"].concat(win.reportBrandCatalogue.map(b => b.id).filter(id => id !== "Lem" && id !== "CSMTEC" && id !== primaryLogo.currentText))
                            onModelChanged: { currentIndex = 0; lem.invalidatePreview(); }
                            onActivated: lem.invalidatePreview()
                        }
                        Row {
                            Layout.alignment: Qt.AlignRight
                            spacing: 12
                            Repeater {
                                model: win.reportBrandCatalogue.filter(b => b.id === primaryLogo.currentText || (secondaryLogo.currentIndex > 0 && b.id === secondaryLogo.currentText)).sort((a, b) => a.id === primaryLogo.currentText ? -1 : 1)
                                Image {
                                    required property var modelData
                                    width: 120; height: 70
                                    fillMode: Image.PreserveAspectFit
                                    source: "data:image/png;base64," + modelData.png
                                }
                            }
                        }
                        UiButton {
                            objectName: "saveReportPresentation"
                            Layout.fillWidth: true
                            text: reportIssuer.saveState.state === "saving" ? "A guardar e confirmar…" : "Guardar formato dos reports"
                            enabled: lem.canSaveReportPresentation
                            onClicked: lem.saveReportPresentation(reportIssuer.currentText, primaryLogo.currentIndex > 0 ? primaryLogo.currentText : "", secondaryLogo.currentIndex > 0 ? secondaryLogo.currentText : "", reportIssuer.presentationRevision)
                        }
                        UiLabel {
                            objectName: "unsavedReportPresentation"
                            visible: reportIssuer.hasUnsavedChanges
                            text: "Alterações por guardar. Prima «Guardar formato dos reports» para aplicar a entidade e os logótipos aos reports deste contrato."
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: "#9a5900"
                        }
                        UiButton {
                            implicitHeight: win.touchHeight
                            text: "Abrir Excel"
                            enabled: !lem.busy && lem.contractId() !== ""
                            onClicked: excelDialog.open()
                        }
                        ExcelImportPanel {
                            id: contractExcelPanel
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            onPreviewRequested: sheet => lem.previewExcel(sheet.name, 1, {},
                                primaryLogo.currentIndex === 0 ? "" : primaryLogo.currentText,
                                secondaryLogo.currentIndex === 0 ? "" : secondaryLogo.currentText,
                                reportIssuer.currentText, reportIssuer.presentationRevision)
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 12
                        visible: adminArea.currentIndex === 2
                        UiLabel {
                            text: "Contas e acesso ao contrato"
                            font.pixelSize: 19
                            font.bold: true
                            Layout.topMargin: 16
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            wrapMode: Text.Wrap
                        }
                        UiLabel {
                            text: "Nome"
                        }
                        UiTextField {
                            id: newName
                            objectName: "newName"
                            implicitHeight: win.touchHeight
                            placeholderText: "Nome"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                        }
                        UiLabel {
                            text: "Email"
                        }
                        UiTextField {
                            id: newEmail
                            objectName: "newEmail"
                            implicitHeight: win.touchHeight
                            placeholderText: "Email"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                        }
                        UiLabel {
                            text: "Password inicial"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            wrapMode: Text.Wrap
                        }
                        UiTextField {
                            id: newPassword
                            objectName: "newPassword"
                            implicitHeight: win.touchHeight
                            placeholderText: "Password inicial (mínimo 12 caracteres)"
                            echoMode: TextInput.Password
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                        }
                        UiLabel {
                            text: "Perfil da nova conta"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            wrapMode: Text.Wrap
                        }
                        UiComboBox {
                            id: newUserRole
                            objectName: "newUserRole"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            model: ["Técnico", "Administrador"]
                            Accessible.name: "Perfil da nova conta"
                        }
                        UiLabel {
                            text: newUserRole.currentIndex === 1 ? "Administradores podem gerir todas as contas e contratos. Confirme o perfil antes de criar." : "Técnicos só têm acesso aos contratos atribuídos."
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            wrapMode: Text.Wrap
                            color: newUserRole.currentIndex === 1 ? win.danger : win.mutedInk
                        }
                        UiButton {
                            objectName: "createUserButton"
                            implicitHeight: win.touchHeight
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: newUserRole.currentIndex === 1 ? "Criar administrador" : "Criar técnico"
                            enabled: lem.authenticated && lem.user.role === "admin" && !lem.busy && newName.text.trim().length > 0 && newEmail.text.indexOf("@") > 0 && newPassword.text.length >= 12
                            onClicked: {
                                lem.createUser(newEmail.text, newName.text, newPassword.text, newUserRole.currentIndex === 1 ? "admin" : "tech");
                                newPassword.clear();
                            }
                        }
                        UiButton {
                            implicitHeight: win.touchHeight
                            text: "Atualizar utilizadores"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            enabled: !lem.busy
                            onClicked: lem.loadUsers()
                        }
                        UiComboBox {
                            id: userBox
                            objectName: "managementUserBox"
                            implicitHeight: win.touchHeight
                            model: lem.users
                            textRole: "label"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                        }
                        UiButton {
                            implicitHeight: win.touchHeight
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: "Atribuir ao contrato"
                            Accessible.name: "Atribuir acesso ao contrato selecionado"
                            enabled: !lem.busy && userBox.currentIndex >= 0 && lem.contractId() !== ""
                            onClicked: lem.assignUser(lem.users[userBox.currentIndex].id)
                        }
                        UiButton {
                            objectName: "resetTechnicianPasswordButton"
                            text: "Repor password do técnico…"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            visible: lem.user.role === "admin"
                            enabled: lem.authenticated && !lem.busy && userBox.currentIndex >= 0
                                     && (lem.users[userBox.currentIndex] || {}).role === "tech"
                            onClicked: {
                                const technician = lem.users[userBox.currentIndex];
                                resetPasswordDialog.userId = technician.id;
                                resetPasswordDialog.userLabel = technician.label;
                                resetPasswordDialog.open();
                            }
                        }
                        UiLabel {
                            text: "O reset de password está disponível apenas para contas de técnicos."
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        UiLabel {
                            objectName: "passwordResetStatus"
                            text: lem.passwordReset.message || ""
                            visible: !!lem.passwordReset.message
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }
                    Item {
                        height: 20
                    }
                    Item {
                        id: adminTail
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(0, win.pageBottomInset - parent.spacing)
                    }
                }
            }
        }

        // A full-height, independent track. Clamp overshoot before passing position
        // to ScrollBar: otherwise Qt shortens the visual thumb during elastic dragging.
        ScrollBar {
            id: pagePositionBar
            objectName: "pagePositionBar"
            parent: win.contentItem
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            z: 50
            width: 6
            padding: 0
            orientation: Qt.Vertical
            visible: win.autoHideChrome && size < 1
            active: visible
            interactive: false
            minimumSize: Math.min(1, 48 / Math.max(1, height))
            size: win.activePageScroll ? Math.min(1, Math.max(minimumSize, win.activePageScroll.height / Math.max(1, win.activePageScroll.contentHeight))) : 1
            position: !win.activePageScroll || win.activePageScroll.atYBeginning ? 0 : win.pageAtRenderedEnd() ? 1 - size : Math.max(0, Math.min(1 - size, win.pageScrollOffset() / Math.max(1, win.activePageScroll.contentHeight - win.activePageScroll.height) * (1 - size)))
            contentItem: Rectangle {
                implicitWidth: 4
                radius: 3
                color: win.mutedInk
            }
            background: Item {}
        }
    }
}
