package org.retroporting.megamanx;

import android.app.AlertDialog;
import android.graphics.*;
import android.os.Bundle;
import android.view.*;
import org.libsdl.app.SDLActivity;

/**
 * SDL owns the runtime. This view adds accessible multitouch game controls.
 */
public final class GameActivity extends SDLActivity {
  static native void nativeInput(int bits);
  static native void nativePause(boolean paused);
  static native void nativeGameQuit();
  static native void nativeSlot(boolean save);
  private boolean menu;
  private TouchControls controls;
  @Override
  protected String[] getArguments() {
    return new String[] {
        new java.io.File(getFilesDir(), "mmx.sfc").getAbsolutePath()};
  }
  @Override
  protected void onCreate(Bundle state) {
    super.onCreate(state);
    if (mLayout != null) {
      controls = new TouchControls();
      mLayout.addView(controls,
                      new android.view.ViewGroup.LayoutParams(-1, -1));
    }
    immersive();
  }
  private void immersive() {
    getWindow().getDecorView().setSystemUiVisibility(
        View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
        View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
        View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
        View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
  }
  @Override
  protected void onPause() {
    nativePause(true);
    super.onPause();
  }
  @Override
  protected void onResume() {
    super.onResume();
    nativePause(menu);
    immersive();
  }
  @Override
  public void onBackPressed() {
    showMenu();
  }
  @Override
  public boolean dispatchKeyEvent(KeyEvent event) {
    if (event.getKeyCode() == KeyEvent.KEYCODE_BACK ||
        event.getKeyCode() == KeyEvent.KEYCODE_ESCAPE) {
      if (event.getAction() == KeyEvent.ACTION_UP)
        showMenu();
      return true;
    }
    return super.dispatchKeyEvent(event);
  }
  @Override
  protected void onDestroy() {
    // SDL joins its native thread here, allowing SRAM and diagnostics to flush.
    // The launcher is a different process; only the game process is disposable.
    super.onDestroy();
    android.os.Process.killProcess(android.os.Process.myPid());
  }
  private void showMenu() {
    if (menu)
      return;
    menu = true;
    nativePause(true);
    AlertDialog dialog =
        new AlertDialog.Builder(this)
            .setTitle("Mega Man X")
            .setItems(new String[] {"Resume game", "Quick save", "Quick load",
                                    "Return to launcher"},
                      (d, which) -> {
                        if (which == 1 || which == 2)
                          nativeSlot(which == 1);
                        if (which == 3)
                          nativeGameQuit();
                      })
            .create();
    dialog.setOnDismissListener(d -> {
      menu = false;
      nativePause(false);
      immersive();
    });
    dialog.show();
  }
  private final class TouchControls extends View {
    final Paint p = new Paint(3);
    boolean visible = true;
    int bits;
    final java.util.ArrayList<RectF> rects = new java.util.ArrayList<>();
    final int[] keys = {1 << 4, 1 << 5,  1 << 6,  1 << 7, 1,      1 << 8,
                        1 << 1, 1 << 9, 1 << 10, 1 << 11, 1 << 3, 1 << 2, 0};
    final String[] labels = {"↑", "↓", "←", "→",     "B",      "A",
                             "Y", "X", "L", "R", "START", "SELECT", "•••"};
    TouchControls() {
      super(GameActivity.this);
      setContentDescription(
          "Touch controls. B jump, Y shoot, A dash, L and R " +
          "change weapons, arrows move. Menu at top right.");
      try {
        visible = !GameFiles.settings(getFilesDir())
                       .getOrDefault("AndroidTouch", "1")
                       .equals("0");
      } catch (Exception ignored) {
      }
    }
    float dp(float n) { return n * getResources().getDisplayMetrics().density; }
    void area(float x, float y, float w, float h) {
      rects.add(new RectF(x, y, x + w, y + h));
    }
    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
      rects.clear();
      float u = Math.min(dp(58), h / 5.2f), m = dp(18), y = h - m - u * 2.6f;
      area(m + u, y, u, u);
      area(m + u, y + u * 1.6f, u, u);
      area(m, y + u * .8f, u, u);
      area(m + u * 2, y + u * .8f, u, u);
      area(w - m - u * 1.1f, h - m - u * 1.1f, u * 1.1f, u * 1.1f);
      area(w - m - u * 1.1f, h - m - u * 2.5f, u * 1.1f, u * 1.1f);
      area(w - m - u * 2.5f, h - m - u * 1.8f, u * 1.1f, u * 1.1f);
      area(w - m - u * 2.5f, h - m - u * 3.2f, u * 1.1f, u * 1.1f);
      area(m, y - u * 1.3f, u * 1.3f, u * .85f);
      area(w - m - u * 1.3f, y - u * 1.3f, u * 1.3f, u * .85f);
      area(w * .5f + dp(8), h - m - u * .8f, u * 1.5f, u * .8f);
      area(w * .5f - u * 1.5f - dp(8), h - m - u * .8f, u * 1.5f, u * .8f);
      area(w - m - dp(64), dp(16), dp(64), dp(48));
    }
    @Override
    protected void onDraw(Canvas c) {
      for (int i = 0; i < rects.size(); i++) {
        if (!visible && i != keys.length - 1)
          continue;
        RectF r = rects.get(i);
        boolean down = (bits & keys[i]) != 0;
        p.setColor(down ? 0xbb6ce1e9 : 0x66203c50);
        p.setStyle(Paint.Style.FILL);
        c.drawRoundRect(r, dp(16), dp(16), p);
        p.setColor(0xbb97d4de);
        p.setStrokeWidth(dp(1));
        p.setStyle(Paint.Style.STROKE);
        c.drawRoundRect(r, dp(16), dp(16), p);
        p.setStyle(Paint.Style.FILL);
        p.setTextAlign(Paint.Align.CENTER);
        p.setTextSize(dp(i >= 9 ? 12 : 22));
        p.setTypeface(Typeface.DEFAULT_BOLD);
        p.setColor(down ? 0xff091421 : 0xddf0fcff);
        c.drawText(labels[i], r.centerX(),
                   r.centerY() - (p.ascent() + p.descent()) / 2, p);
      }
    }
    @Override
    public boolean onTouchEvent(android.view.MotionEvent e) {
      int action = e.getActionMasked();
      if (action == MotionEvent.ACTION_DOWN &&
          rects.get(keys.length - 1).contains(e.getX(), e.getY())) {
        showMenu();
        return true;
      }
      bits = 0;
      if (visible && action != MotionEvent.ACTION_CANCEL &&
          action != MotionEvent.ACTION_UP)
        for (int j = 0; j < e.getPointerCount(); j++) {
          if (action == MotionEvent.ACTION_POINTER_UP &&
              j == e.getActionIndex())
            continue;
          for (int i = 0; i < keys.length - 1; i++)
            if (rects.get(i).contains(e.getX(j), e.getY(j)))
              bits |= keys[i];
        }
      nativeInput(bits);
      invalidate();
      if (action == MotionEvent.ACTION_UP)
        performClick();
      return true;
    }
    @Override
    public boolean performClick() {
      super.performClick();
      return true;
    }
  }
}
