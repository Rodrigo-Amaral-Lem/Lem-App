// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

// Build-only import anchor. Included only by the Windows standalone target.
// Qt's QML import scanner must see the style selected in C++ at runtime.
import QtQuick
import QtQuick.Controls.Fusion as FusionStyle
import QtQuick.Controls.Basic as BasicStyle
QtObject {}
