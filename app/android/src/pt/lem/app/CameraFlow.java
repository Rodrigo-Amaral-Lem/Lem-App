package pt.lem.app;

/** Lifecycle state for normal camera -> explicit evidence picker, without media scanning. */
final class CameraFlow {
    static final int READY = 0, CAMERA = 1, PICKER = 2, CAPTURE = 3, FINISHED = 4;
    static final int NONE = 0, OPEN_CAMERA = 1, OPEN_PICKER = 2;
    int state = READY;
    boolean cameraLeft;

    int resume() {
        if (state == READY) {
            state = CAMERA;
            return OPEN_CAMERA;
        }
        if (state == CAMERA && cameraLeft) {
            state = PICKER;
            return OPEN_PICKER;
        }
        return NONE;
    }

    void pause() { if (state == CAMERA) cameraLeft = true; }
}
