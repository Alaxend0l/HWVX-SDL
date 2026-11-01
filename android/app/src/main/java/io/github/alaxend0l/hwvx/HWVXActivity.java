package io.github.alaxend0l.hwvx;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;

import org.libsdl.app.SDLActivity;

import java.io.File;

/**
 * SDL's activity, plus access to the game data. The game looks for the
 * extracted disc (HW_share.xgc, worlds_/, audio_/ ...) in the app's own
 * folder (Android/data/io.github.alaxend0l.hwvx/files/files, no permission
 * needed) and in /sdcard/HWVX (see FindDataDir in src/port/main_sdl.c).
 * The second needs storage access, asked for here when the data isn't in
 * the first.
 */
public class HWVXActivity extends SDLActivity {
    private static final String MARKER = "HW_share.xgc";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (dataInAppFolder()) {
            return;
        }
        if (Build.VERSION.SDK_INT >= 30) {
            if (!Environment.isExternalStorageManager()) {
                // "All files access" is a settings page, not a dialog. The
                // game reports missing data until it is granted and the app
                // is restarted.
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                           Uri.parse("package:" + getPackageName()));
                try {
                    startActivity(intent);
                } catch (Exception e) {
                    startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                }
            }
        } else if (checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
                   != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] { Manifest.permission.READ_EXTERNAL_STORAGE }, 1);
        }
    }

    private boolean dataInAppFolder() {
        File dir = getExternalFilesDir(null);
        return dir != null && (new File(dir, "files/" + MARKER).exists()
                               || new File(dir, "files/files/" + MARKER).exists());
    }
}
