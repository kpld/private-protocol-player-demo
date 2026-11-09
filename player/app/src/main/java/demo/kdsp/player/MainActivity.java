package demo.kdsp.player;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;

import java.io.IOException;
import java.util.Locale;

import tv.danmaku.ijk.media.player.IMediaPlayer;
import tv.danmaku.ijk.media.player.IjkMediaPlayer;

/**
 * Minimal KDSP demo player.
 *
 * Skeleton scope: playback via ijkplayer, first-frame timing, elapsed time,
 * video size, reconnect counting, and a stats overlay. The dropped-frame /
 * bitrate counters that come from the KDSP protocol layer are wired as TODOs
 * below — they become available once the custom ijkplayer build with the
 * kdsp protocol (player/ffmpeg-patch/) is used instead of the stock 0.8.8
 * artifacts.
 *
 * NOTE: not yet compiled — this machine has no Android SDK/JDK. First build
 * happens with the custom ijkplayer build (player/ijkplayer-build.md);
 * verify ijkplayer API constant names against the k0.8.8 sources there.
 */
public class MainActivity extends Activity implements SurfaceHolder.Callback {

    private EditText urlInput;
    private TextView statsView;
    private SurfaceView surfaceView;

    private IjkMediaPlayer player;
    private SurfaceHolder holder;
    private final Handler ui = new Handler(Looper.getMainLooper());

    private long playStartElapsed;
    private long firstFrameMs = -1;
    private int videoWidth, videoHeight;
    private int reconnectCount;
    private boolean playing;

    private final Runnable statsTicker = new Runnable() {
        @Override public void run() {
            renderStats();
            if (playing) ui.postDelayed(this, 500);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        urlInput = findViewById(R.id.url_input);
        statsView = findViewById(R.id.stats);
        surfaceView = findViewById(R.id.surface);
        surfaceView.getHolder().addCallback(this);

        Button play = findViewById(R.id.btn_play);
        Button stop = findViewById(R.id.btn_stop);
        play.setOnClickListener(v -> startPlay(urlInput.getText().toString().trim()));
        stop.setOnClickListener(v -> stopPlay());
    }

    private void startPlay(String url) {
        stopPlay();
        if (holder == null || url.isEmpty()) return;

        player = new IjkMediaPlayer();
        // Live-stream tuning used by the demo: low probing, reconnect on.
        player.setOption(IjkMediaPlayer.OPT_CATEGORY_FORMAT, "reconnect", 1);
        player.setOption(IjkMediaPlayer.OPT_CATEGORY_FORMAT, "protocol_whitelist",
                "kdsp,crypto,file,http,https,tcp,tls,udp");
        player.setDisplay(holder);
        player.setOnInfoListener((mp, what, extra) -> {
            if (what == IMediaPlayer.MEDIA_INFO_VIDEO_RENDERING_START && firstFrameMs < 0) {
                firstFrameMs = SystemClock.elapsedRealtime() - playStartElapsed;
                renderStats();
            }
            return false;
        });
        player.setOnVideoSizeChangedListener((mp, w, h, sarNum, sarDen) -> {
            videoWidth = w;
            videoHeight = h;
            renderStats();
        });
        player.setOnErrorListener((mp, what, extra) -> {
            // Skeleton policy: count and retry a bounded number of times.
            // The production pattern (backoff, surface the error to the user)
            // is part of the write-up, not this demo shell.
            if (playing && reconnectCount < 10) {
                reconnectCount++;
                ui.postDelayed(() -> startPlay(url), 1000);
            }
            return true;
        });
        try {
            player.setDataSource(url);
        } catch (IOException e) {
            statsView.setText("setDataSource failed: " + e.getMessage());
            return;
        }
        playStartElapsed = SystemClock.elapsedRealtime();
        firstFrameMs = -1;
        playing = true;
        player.prepareAsync();
        player.start();
        ui.post(statsTicker);
    }

    private void stopPlay() {
        playing = false;
        ui.removeCallbacks(statsTicker);
        if (player != null) {
            try { player.stop(); } catch (Exception ignored) { }
            player.release();
            player = null;
        }
    }

    private void renderStats() {
        long elapsed = playing ? (SystemClock.elapsedRealtime() - playStartElapsed) / 1000 : 0;
        String first = firstFrameMs >= 0 ? (firstFrameMs + " ms") : "—";
        String size = videoWidth > 0 ? (videoWidth + "x" + videoHeight) : "—";
        // TODO(kdsp protocol): dropped frames / jitter / bitrate counters from
        // the protocol layer, once the custom build is in use. Placeholder
        // values are shown as n/a on purpose — never fake stats in a demo
        // whose whole point is credibility.
        statsView.setText(String.format(Locale.US,
                "state: %s\nfirst frame: %s\nelapsed: %ds\nvideo: %s\nreconnects: %d\ndropped frames: n/a (needs kdsp build)\nbitrate: n/a (needs kdsp build)",
                playing ? "playing" : "idle", first, elapsed, size, reconnectCount));
    }

    @Override public void surfaceCreated(SurfaceHolder h) { holder = h; }
    @Override public void surfaceChanged(SurfaceHolder h, int f, int w, int ht) { }
    @Override public void surfaceDestroyed(SurfaceHolder h) { stopPlay(); holder = null; }

    @Override
    protected void onDestroy() {
        stopPlay();
        super.onDestroy();
    }
}
