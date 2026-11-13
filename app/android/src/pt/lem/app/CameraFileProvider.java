// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

package pt.lem.app;

/** Separate provider identity from Qt's existing document-sharing provider. */
public class CameraFileProvider extends androidx.core.content.FileProvider {
    public CameraFileProvider() {
        super();
    }
}
