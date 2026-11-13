// Android-only camera bridge. Open the normal camera, then choose evidence;
// return report/checkpoint identity through an atomic JSON file mailbox read by Qt.
// See docs/developer/platform-and-deployment.md for ownership of temporary files.

package pt.lem.app;

import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.provider.MediaStore;
import androidx.core.content.FileProvider;
import org.json.JSONObject;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/** Normal camera/picker flow, with capture-intent fallback. No broad media permissions. */
public class CaptureActivity extends Activity {
    private static final int CAMERA_REQUEST = 2417;
    private File photo;
    private Uri photoUri;
    private String reportId;
    private String checkpoint;
    private static final int PICK_REQUEST = 2418;
    private final CameraFlow flow = new CameraFlow();
    private boolean video;

    public static boolean launch(Activity activity, String reportId, String checkpoint, boolean video) {
        try {
            Intent request = new Intent(activity, CaptureActivity.class);
            request.putExtra("reportId", reportId);
            request.putExtra("checkpoint", checkpoint);
            request.putExtra("video", video);
            activity.startActivity(request);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        reportId = getIntent().getStringExtra("reportId");
        checkpoint = getIntent().getStringExtra("checkpoint");
        video = getIntent().getBooleanExtra("video", false);
        if (state != null) {
            String path = state.getString("photo");
            if (path != null)
                photo = new File(path);
            String uri = state.getString("uri");
            if (uri != null)
                photoUri = Uri.parse(uri);
            flow.state = state.getInt("flow", CameraFlow.CAPTURE);
            flow.cameraLeft = state.getBoolean("cameraLeft") || flow.state == CameraFlow.CAMERA;
            if (photo != null && photoUri != null)
                return; // Preserve the active camera/picker across process recreation.
        }
        try {
            File directory = new File(getCacheDir(), "camera");
            if (!directory.isDirectory() && !directory.mkdirs())
                throw new java.io.IOException("Não foi possível preparar a fotografia.");
            photo = File.createTempFile("capture-", video ? ".mp4" : ".jpg", directory);
            photoUri = FileProvider.getUriForFile(this, getPackageName() + ".camera", photo);
        } catch (Exception e) {
            finishCapture("Não foi possível preparar a evidência. Use a galeria.", false);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        int action = flow.resume();
        if (action == CameraFlow.OPEN_CAMERA) {
            try {
                // Normal camera mode retains the camera vendor's normal UI/gallery.
                // It does not return a captured file: the user selects evidence on return.
                Intent camera = new Intent(video ? MediaStore.INTENT_ACTION_VIDEO_CAMERA
                                                 : MediaStore.INTENT_ACTION_STILL_IMAGE_CAMERA);
                startActivity(camera);
                android.widget.Toast.makeText(this,
                    video ? "Grave até 60 s / 20 MB. Depois volte à Lem e selecione o vídeo."
                          : "Tire a fotografia. Depois volte à Lem e selecione a imagem.",
                    android.widget.Toast.LENGTH_LONG).show();
            } catch (android.content.ActivityNotFoundException e) {
                openCaptureFallback();
            } catch (Exception e) {
                finishCapture("Não foi possível abrir a câmara. Use a galeria.", false);
            }
        } else if (action == CameraFlow.OPEN_PICKER) {
            openEvidencePicker();
        }
    }

    @Override
    protected void onPause() {
        flow.pause();
        super.onPause();
    }

    private void openEvidencePicker() {
        try {
            Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            picker.addCategory(Intent.CATEGORY_OPENABLE);
            picker.setType(video ? "video/*" : "image/*");
            startActivityForResult(picker, PICK_REQUEST);
        } catch (Exception e) {
            finishCapture("Não foi possível abrir a galeria. Use os botões de galeria na Lem.", false);
        }
    }

    private void openCaptureFallback() {
        try {
            flow.state = CameraFlow.CAPTURE;
            Intent camera = new Intent(video ? MediaStore.ACTION_VIDEO_CAPTURE : MediaStore.ACTION_IMAGE_CAPTURE);
            if (video) {
                camera.putExtra(MediaStore.EXTRA_DURATION_LIMIT, 60);
                camera.putExtra(MediaStore.EXTRA_SIZE_LIMIT, 20L * 1024 * 1024);
                camera.putExtra(MediaStore.EXTRA_VIDEO_QUALITY, 0);
            }
            camera.putExtra(MediaStore.EXTRA_OUTPUT, photoUri);
            camera.setClipData(ClipData.newRawUri("Lem photo", photoUri));
            camera.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION |
                            Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivityForResult(camera, CAMERA_REQUEST);
        } catch (android.content.ActivityNotFoundException e) {
            finishCapture(
                "Não existe uma aplicação de câmara disponível. Use a galeria.",
                false);
        } catch (Exception e) {
            finishCapture("Não foi possível abrir a câmara. Use a galeria.",
                          false);
        }
    }

    @Override
    public void onSaveInstanceState(Bundle state) {
        state.putString("photo", photo == null ? null : photo.getAbsolutePath());
        state.putString("uri", photoUri == null ? null : photoUri.toString());
        state.putInt("flow", flow.state);
        state.putBoolean("cameraLeft", flow.cameraLeft);
        super.onSaveInstanceState(state);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if ((request != CAMERA_REQUEST && request != PICK_REQUEST) || flow.state == CameraFlow.FINISHED)
            return;
        if (result != RESULT_OK) {
            finishCapture("Captura cancelada.", false);
        } else {
            // Some system cameras ignore EXTRA_OUTPUT and return their own URI.
            // Copy only into our temporary file; never remove the gallery original.
            try {
                if (request == PICK_REQUEST && (data == null || data.getData() == null))
                    throw new java.io.IOException("No selected evidence");
                if (photo != null && photo.length() == 0 && data != null && data.getData() != null && !data.getData().equals(photoUri)) {
                    try (InputStream input = getContentResolver().openInputStream(data.getData());
                         FileOutputStream output = new FileOutputStream(photo)) {
                        byte[] buffer = new byte[65536]; long total = 0; int count;
                        if (input == null) throw new java.io.IOException("No camera data");
                        while ((count = input.read(buffer)) != -1) {
                            total += count;
                            if (total > 20L * 1024 * 1024) throw new java.io.IOException("Evidence too large");
                            output.write(buffer, 0, count);
                        }
                    }
                }
                if (photo == null || !photo.isFile() || photo.length() == 0 || photo.length() > 20L * 1024 * 1024)
                    throw new java.io.IOException("Invalid capture");
                finishCapture("", true);
            } catch (Exception error) {
                finishCapture("Captura inválida ou superior a 20 MB. Tente uma captura mais curta.", false);
            }
        }
    }

    private void finishCapture(String error, boolean success) {
        if (flow.state == CameraFlow.FINISHED) return;
        flow.state = CameraFlow.FINISHED;
        // A file mailbox survives Activity recreation; Qt reads it on returning to Active.
        // Keep a successful source photo until Qt confirms it was copied to the report.
        try {
            JSONObject result = new JSONObject();
            result.put("reportId", reportId == null ? "" : reportId);
            result.put("checkpoint", checkpoint == null ? "" : checkpoint);
            result.put("path", success ? photo.getAbsolutePath() : "");
            result.put("error", error);
            File temporary = new File(getCacheDir(), "camera-result.tmp");
            try (FileOutputStream output = new FileOutputStream(temporary)) {
                output.write(result.toString().getBytes(StandardCharsets.UTF_8));
                output.getFD().sync();
            }
            File destination = new File(getCacheDir(), "camera-result.json");
            if (!temporary.renameTo(destination))
                throw new java.io.IOException("Camera result save failed");
        } catch (Exception e) {
            // Do not delete a successful capture if result delivery fails.
            android.widget.Toast
                .makeText(this, "Não foi possível entregar a evidência à app.",
                          android.widget.Toast.LENGTH_LONG)
                .show();
        } finally {
            if (photoUri != null)
                revokeUriPermission(photoUri, Intent.FLAG_GRANT_WRITE_URI_PERMISSION |
                                                  Intent.FLAG_GRANT_READ_URI_PERMISSION);
            if (!success && photo != null)
                photo.delete();
            finish();
        }
    }
}
