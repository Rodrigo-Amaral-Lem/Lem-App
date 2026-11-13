package pt.lem.app;

/** Separate provider identity from Qt's existing document-sharing provider. */
public class CameraFileProvider extends androidx.core.content.FileProvider {
    public CameraFileProvider() {
        super();
    }
}
