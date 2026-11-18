package org.retroporting.megamanx;

import android.app.*;
import android.content.*;
import android.graphics.*;
import android.graphics.drawable.GradientDrawable;
import android.os.*;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;

/**
 * Native Android launcher: large touch targets, responsive layout, persistent
 * mods.
 */
public final class LauncherActivity extends Activity {
  static final int BG = 0xff091421, CARD = 0xff142639, TEXT = 0xfff2f5fa,
                   MUTED = 0xffaebfd0, CYAN = 0xff6ce1e9, GOLD = 0xfff4c665;
  private final ExecutorService io = Executors.newSingleThreadExecutor();
  private LinearLayout shell, body;
  private String tab = "Home";
  private boolean ready, rom;
  private String status = "Preparing Hunter HQ…";
  private boolean compact() { return getResources().getConfiguration().orientation == android.content.res.Configuration.ORIENTATION_LANDSCAPE && getResources().getConfiguration().screenHeightDp < 400; }
  int dp(float v) {
    return Math.round(v * getResources().getDisplayMetrics().density);
  }
  @Override
  public void onCreate(Bundle state) {
    super.onCreate(state);
    getWindow().setStatusBarColor(BG);
    getWindow().setNavigationBarColor(BG);
    draw();
    prepare();
  }
  @Override
  protected void onResume() {
    super.onResume();
    if (ready)
      prepare();
  }
  @Override
  public void onConfigurationChanged(android.content.res.Configuration c) {
    super.onConfigurationChanged(c);
    draw();
  }
  private void prepare() {
    io.execute(() -> {
      try {
        GameFiles.seed(getFilesDir());
        try (InputStream bundled = getAssets().open("content.zip")) {
          GameFiles.content(getFilesDir(), bundled, BuildConfig.VERSION_NAME);
        }
        rom = GameFiles.hasRom(getFilesDir());
        ready = true;
        status = rom ? "Mega Man X - Ready to play"
                     : "Choose your USA Rev 1 ROM";
      } catch (Exception e) {
        ready = false;
        status = e.getMessage();
      }
      runOnUiThread(this::draw);
    });
  }
  private GradientDrawable background(int color, int stroke) {
    GradientDrawable d = new GradientDrawable();
    d.setColor(color);
    d.setCornerRadius(dp(18));
    if (stroke != 0)
      d.setStroke(dp(1), stroke);
    return d;
  }
  TextView text(String value, int size, int color) {
    TextView v = new TextView(this);
    v.setText(value);
    v.setTextSize(size);
    v.setTextColor(color);
    v.setPadding(0, dp(compact()?2:5), 0, dp(compact()?2:5));
    return v;
  }
  LinearLayout column() {
    LinearLayout v = new LinearLayout(this);
    v.setOrientation(LinearLayout.VERTICAL);
    return v;
  }
  LinearLayout card() {
    LinearLayout c = column();
    c.setPadding(dp(compact()?12:20), dp(compact()?8:15), dp(compact()?12:20), dp(compact()?10:18));
    c.setBackground(background(CARD, 0xff284258));
    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
    lp.bottomMargin = dp(compact()?10:16);
    c.setLayoutParams(lp);
    body.addView(c);
    return c;
  }
  Button button(String label, boolean primary, Runnable action) {
    Button b = new Button(this);
    b.setText(label);
    b.setAllCaps(false);
    b.setTextSize(17);
    b.setTextColor(primary ? BG : TEXT);
    b.setMinHeight(dp(compact()?48:56));
    b.setBackground(background(primary ? CYAN : 0xff20374b, 0));
    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, dp(compact()?48:56));
    lp.topMargin = dp(compact()?8:12);
    b.setLayoutParams(lp);
    b.setOnClickListener(v -> action.run());
    return b;
  }
  private void draw() {
    shell = column();
    shell.setBackgroundColor(BG);
    shell.setPadding(dp(compact()?12:20), dp(compact()?6:12), dp(compact()?12:20), dp(compact()?6:12));
    if (Build.VERSION.SDK_INT >= 30)
      shell.setOnApplyWindowInsetsListener((v, insets) -> {
        android.graphics.Insets i = insets.getInsets(
            WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
        v.setPadding(dp(compact()?12:20) + i.left, dp(compact()?6:12) + i.top, dp(compact()?12:20) + i.right,
                     dp(compact()?6:12) + i.bottom);
        return insets;
      });
    TextView brand = text("MEGA MAN X", compact()?14:18, CYAN);
    brand.setLetterSpacing(.15f);
    brand.setTypeface(null, Typeface.BOLD);
    shell.addView(brand);
    ScrollView scroll = new ScrollView(this);
    scroll.setFillViewport(true);
    shell.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
    body = column();
    body.setPadding(0, dp(compact()?6:12), 0, dp(compact()?4:8));
    scroll.addView(body);
    if (tab.equals("Home"))
      home();
    else if (tab.equals("Settings"))
      settings();
    else
      mods();
    LinearLayout nav = new LinearLayout(this);
    for (String name : new String[] {"Home", "Settings", "Mods"}) {
      Button b = button(name, false, () -> {
        tab = name;
        draw();
      });
      b.setTextColor(tab.equals(name) ? CYAN : MUTED);
      LinearLayout.LayoutParams lp =
          new LinearLayout.LayoutParams(0, dp(compact()?48:56), 1);
      lp.setMargins(dp(3), 0, dp(3), 0);
      nav.addView(b, lp);
    }
    shell.addView(nav);
    setContentView(shell);
  }
  private void home() {
    boolean wide = getResources().getConfiguration().orientation ==
        android.content.res.Configuration.ORIENTATION_LANDSCAPE;
    LinearLayout hero = card();
    LinearLayout visual = wide ? column() : hero;
    LinearLayout actions = wide ? column() : hero;
    if (wide) {
      hero.setOrientation(LinearLayout.HORIZONTAL);
      visual.setPadding(0, 0, dp(20), 0);
      hero.addView(visual, new LinearLayout.LayoutParams(0, -2, 1));
      hero.addView(actions, new LinearLayout.LayoutParams(0, -2, 1));
    }
    TextView tag = text("MAVERICK HUNTER HQ", compact()?10:12, GOLD);
    tag.setLetterSpacing(.14f);
    visual.addView(tag);
    boolean smallPortrait = !wide && getResources().getConfiguration().screenHeightDp < 650;
    TextView title = text(smallPortrait ? "Ready, Hunter?" : "Suit up.\nReady, Hunter?", wide ? 24 : smallPortrait ? 26 : 34, TEXT);
    title.setTypeface(null, Typeface.BOLD);
    visual.addView(title);
    View track = new View(this) {
      final Paint p = new Paint(3);
      @Override
      protected void onDraw(Canvas c) {
        super.onDraw(c);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(dp(2));
        for (int j = 0; j < 6; j++) {
          p.setColor(j == 0 ? CYAN : 0xff29465d);
          Path path = new Path();
          path.moveTo(-dp(20), getHeight() - dp(j * 10));
          path.lineTo(getWidth() * .30f, getHeight() - dp(j * 10));
          path.lineTo(getWidth() * .55f, dp(j * 11));
          path.lineTo(getWidth() + dp(20), dp(j * 11));
          c.drawPath(path, p);
        }
      }
    };
    if (!smallPortrait) visual.addView(track, new LinearLayout.LayoutParams(-1, dp(wide ? 36 : 95)));
    actions.addView(text(status, compact()?13:15, MUTED));
    Button play = button(rom ? "Play Mega Man X" : "Choose ROM", true, () -> {
      if (rom) {
        startActivity(new Intent(this, GameActivity.class));
      } else
        pickRom();
    });
    play.setEnabled(ready);
    actions.addView(play);
    if (rom)
      actions.addView(button("Change ROM", false, this::pickRom));
    LinearLayout info = card();
    info.addView(text("Made for your screen", 20, TEXT));
    info.addView(text("Adaptive widescreen follows your display. Play with " +
                      "touch controls or a connected gamepad. Open the pause " +
                      "menu any time to save, load or return here.",
                      15, MUTED));
    info.addView(
        text("No game ROM is included. Import your own unmodified USA dump. " +
             "Your passwords, settings and saves stay on this device.",
             13, MUTED));
  }
  private void pickRom() {
    Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
    i.addCategory(Intent.CATEGORY_OPENABLE);
    i.setType("*/*");
    startActivityForResult(i, 41);
  }
  @Override
  protected void onActivityResult(int request, int result, Intent data) {
    super.onActivityResult(request, result, data);
    if (request != 41 || result != RESULT_OK || data == null)
      return;
    ready = false;
    status = "Checking your ROM…";
    draw();
    io.execute(() -> {
      try (InputStream in =
               getContentResolver().openInputStream(data.getData())) {
        GameFiles.replace(new File(getFilesDir(), "mmx.sfc"),
                          GameFiles.readRom(in));
        runOnUiThread(this::prepare);
      } catch (Exception e) {
        ready = true;
        runOnUiThread(() -> {
          new AlertDialog.Builder(this)
              .setTitle("ROM not imported")
              .setMessage(e.getMessage())
              .setPositiveButton("OK", null)
              .show();
          prepare();
        });
      }
    });
  }
  private Map<String, String> values() {
    try {
      return GameFiles.settings(getFilesDir());
    } catch (Exception e) {
      error(e);
      return new HashMap<>();
    }
  }
  private void error(Exception e) {
    new AlertDialog.Builder(this)
        .setTitle("Unable to save")
        .setMessage(e.getMessage())
        .setPositiveButton("OK", null)
        .show();
  }
  private void save(String key, String value) {
    try {
      GameFiles.setting(getFilesDir(), key, value);
    } catch (Exception e) {
      error(e);
    }
  }
  private void toggle(String title, String detail, String key, String on,
                      String fallback) {
    LinearLayout c = card();
    Switch s = new Switch(this);
    s.setText(title);
    s.setTextSize(18);
    s.setTextColor(TEXT);
    s.setMinHeight(dp(52));
    s.setChecked(!values().getOrDefault(key, fallback).equals("0"));
    c.addView(s);
    c.addView(text(detail, 14, MUTED));
    s.setOnCheckedChangeListener((v, checked) -> save(key, checked ? on : "0"));
  }
  private void choice(String title, String detail, String key, String[] labels,
                      String[] entries, String fallback) {
    LinearLayout c = card();
    c.addView(text(title, 19, TEXT));
    c.addView(text(detail, 14, MUTED));
    Spinner spinner = new Spinner(this);
    ArrayAdapter<String> a = new ArrayAdapter<>(
        this, android.R.layout.simple_spinner_dropdown_item, labels);
    spinner.setAdapter(a);
    String value = values().getOrDefault(key, fallback);
    for (int j = 0; j < entries.length; j++)
      if (entries[j].equals(value))
        spinner.setSelection(j);
    c.addView(spinner, new LinearLayout.LayoutParams(-1, dp(compact()?48:56)));
    spinner.setOnItemSelectedListener(
        new android.widget.AdapterView.OnItemSelectedListener() {
          public void onNothingSelected(android.widget.AdapterView<?> p) {}
          public void onItemSelected(android.widget.AdapterView<?> p, View v,
                                     int position, long id) {
            if (!values().getOrDefault(key, fallback).equals(entries[position]))
              save(key, entries[position]);
          }
        });
  }
  private void settings() {
    body.addView(text("Your play setup",29,TEXT));
    body.addView(text("Saved on this device. Changes apply to your next game.",14,MUTED));
    choice("Screen shape","Fit to screen expands the stage view to your display.","Aspect",
      new String[]{"Fit to screen","Original 4:3","16:9","21:9","32:9"},
      new String[]{"adaptive","4:3","16:9","21:9","32:9"},"adaptive");
    toggle("Touch controls","Move, jump and shoot together. A connected controller works alongside touch.","AndroidTouch","1","1");
    LinearLayout c=card();
    c.addView(text("Controller layout",20,TEXT));
    c.addView(text("D-pad / left stick: move\nB: jump \u2022 Y: shoot \u2022 A: dash\nL / R: change weapons\nStart: pause \u2022 Android Back: game menu",16,MUTED));
    c.addView(text("Mega Man X "+BuildConfig.VERSION_NAME+" \u2022 Android",13,CYAN));
  }
  private void mods() {
    body.addView(text("Make it your mission",29,TEXT));
    body.addView(text("Independent options. Your choices are remembered.",14,MUTED));
    toggle("Widescreen","Expand stage scenery and enemies with an anchored HUD.","Widescreen","1","1");
    toggle("Boss Rush","Add the optional two-boss challenge menu alongside the original campaign.","BossRush","1","1");
    toggle("Crash diagnostics and replay","Record gameplay inputs and checkpoints to help investigate problems. This works independently of presentation mods.","CrashReplay","1","1");
    LinearLayout c=card();
    c.addView(text("Classic X on the go",20,TEXT));
    c.addView(text("This Android release includes the classic Mega Man X campaign and Boss Rush. Desktop releases continue to offer the full mod catalog, source-ROM imports and online lobby.",14,MUTED));
  }
}
