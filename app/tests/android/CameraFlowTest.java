// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

package pt.lem.app;

/** Runs on a normal JVM; no Android services or media are accessed. */
public final class CameraFlowTest {
    private static void require(boolean condition) {
        if (!condition) throw new AssertionError("Unexpected camera lifecycle transition");
    }
    public static void main(String[] args) {
        CameraFlow flow = new CameraFlow();
        require(flow.resume() == CameraFlow.OPEN_CAMERA);
        require(flow.resume() == CameraFlow.NONE); // No duplicate onResume launch.
        flow.pause();
        require(flow.resume() == CameraFlow.OPEN_PICKER);
        require(flow.resume() == CameraFlow.NONE);
        flow.pause();
        require(flow.resume() == CameraFlow.NONE); // Returning from picker does not relaunch.
        CameraFlow restored = new CameraFlow();
        restored.state = CameraFlow.CAMERA;
        restored.cameraLeft = true;
        require(restored.resume() == CameraFlow.OPEN_PICKER);
        restored.state = CameraFlow.PICKER;
        require(restored.resume() == CameraFlow.NONE);
        restored.state = CameraFlow.CAPTURE;
        restored.pause();
        require(restored.resume() == CameraFlow.NONE); // Capture fallback uses its result callback.
        restored.state = CameraFlow.FINISHED;
        restored.pause();
        require(restored.resume() == CameraFlow.NONE);
        System.out.println("PASS: camera/picker lifecycle, recreation, fallback and completion");
    }
}
