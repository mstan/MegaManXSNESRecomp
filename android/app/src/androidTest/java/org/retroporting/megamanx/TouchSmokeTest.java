package org.retroporting.megamanx;

import static org.junit.Assert.*;

import android.app.Instrumentation;
import android.app.UiAutomation;
import android.os.SystemClock;
import android.util.DisplayMetrics;
import android.view.*;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.*;
import java.util.*;
import org.junit.Test;

/**
 * Run with a test-owned game active. Verify input after native guest
 * filtering.
 */
public final class TouchSmokeTest {
  private Instrumentation getInstrumentation() {
    return InstrumentationRegistry.getInstrumentation();
  }
  private File[] segments() {
    File root=new File(getInstrumentation().getTargetContext().getFilesDir(),"logs");
    File[] files=root.listFiles(f -> f.getName().startsWith("mmx-replay-") && f.getName().endsWith(".replay"));
    if(files==null) return new File[0];
    Arrays.sort(files,Comparator.comparingLong(File::lastModified));
    return files;
  }
  private String newestSession() {
    File[] files=segments();
    return files.length==0 ? "" : files[files.length-1].getName();
  }
  private List<long[]> samples() throws Exception {
    File[] files=segments(); List<long[]> rows=new ArrayList<>();
    if(files.length==0) return rows;
    try(BufferedReader in=new BufferedReader(new FileReader(files[files.length-1]))) {
      String line;
      while((line=in.readLine())!=null) {
        String[] columns=line.split(" ");
        if(columns.length>=3 && columns[0].equals("I"))
          rows.add(new long[]{Long.parseLong(columns[1]),Long.parseLong(columns[2],16)});
      }
    }
    return rows;
  }
  private void touch(UiAutomation ui, long time, int action, float[][] points) {
    MotionEvent.PointerProperties[] props =
        new MotionEvent.PointerProperties[points.length];
    MotionEvent.PointerCoords[] coords =
        new MotionEvent.PointerCoords[points.length];
    for (int i = 0; i < points.length; i++) {
      props[i] = new MotionEvent.PointerProperties();
      props[i].id = i;
      props[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
      coords[i] = new MotionEvent.PointerCoords();
      coords[i].x = points[i][0];
      coords[i].y = points[i][1];
      coords[i].pressure = 1;
      coords[i].size = 1;
    }
    MotionEvent event = MotionEvent.obtain(
        time, SystemClock.uptimeMillis(), action, points.length, props, coords,
        0, 0, 1, 1, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0);
    assertTrue(ui.injectInputEvent(event, true));
    event.recycle();
  }
  @Test
  public void shootAndMoveTogether() throws Exception {
    String previous = newestSession();
    android.content.Intent game = new android.content.Intent();
    game.setClassName("org.retroporting.megamanx",
                      "org.retroporting.megamanx.GameActivity");
    game.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK);
    getInstrumentation().getTargetContext().startActivity(game);
    // Readiness is native frame evidence, with no elapsed-time kill cutoff.
    while (newestSession().equals(previous) || samples().size() < 3)
      SystemClock.sleep(100);
    List<long[]> before = samples();
    assertFalse(before.isEmpty());
    long start = before.get(before.size() - 1)[0];
    DisplayMetrics metrics = new DisplayMetrics();
    ((WindowManager)getInstrumentation().getTargetContext().getSystemService(
         "window"))
        .getDefaultDisplay()
        .getRealMetrics(metrics);
    float w = metrics.widthPixels, h = metrics.heightPixels,
          d = metrics.density, u = Math.min(58 * d, h / 5.2f), margin = 18 * d;
    float[] accelerate = {w - margin - u * 1.95f, h - margin - u * 1.25f};
    float[] left = {margin + u * 2.5f, h - margin - u * 2.6f + u * 1.3f};
    UiAutomation ui = getInstrumentation().getUiAutomation();
    long down = SystemClock.uptimeMillis();
    touch(ui, down, MotionEvent.ACTION_DOWN, new float[][] {accelerate});
    touch(ui, down,
          MotionEvent.ACTION_POINTER_DOWN |
              (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
          new float[][] {accelerate, left});
    SystemClock.sleep(700);
    touch(ui, down,
          MotionEvent.ACTION_POINTER_UP |
              (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
          new float[][] {accelerate, left});
    touch(ui, down, MotionEvent.ACTION_UP, new float[][] {accelerate});
    SystemClock.sleep(300);
    boolean combined = false, released = false;
    for (long[] row : samples())
      if (row[0] > start) {
        int pad = (int)row[1] & 4095;
        if ((pad & 130) == 130)
          combined = true;
        if (combined && pad == 0)
          released = true;
      }
    assertTrue("Native game must receive shoot and right together",
               combined);
    assertTrue("Releasing both fingers must clear native input", released);
  }

  private long frame() throws Exception {
    List<long[]> rows=samples(); return rows.isEmpty()?-1:rows.get(rows.size()-1)[0];
  }
  private void waitFrames(long count) throws Exception {
    long target=frame()+count;
    while(frame()<target) SystemClock.sleep(100);
  }
  private void startForFrames(UiAutomation ui,float x,float y) throws Exception {
    long down=SystemClock.uptimeMillis();
    touch(ui,down,MotionEvent.ACTION_DOWN,new float[][]{{x,y}});
    waitFrames(30);
    touch(ui,down,MotionEvent.ACTION_UP,new float[][]{{x,y}});
  }
  @Test public void classicCampaignStarts() throws Exception {
    String previous=newestSession();
    android.content.Intent game=new android.content.Intent();
    game.setClassName("org.retroporting.megamanx","org.retroporting.megamanx.GameActivity");
    game.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK);
    getInstrumentation().getTargetContext().startActivity(game);
    while(newestSession().equals(previous)||samples().size()<3) SystemClock.sleep(100);
    waitFrames(1400);
    DisplayMetrics metrics=new DisplayMetrics();
    ((WindowManager)getInstrumentation().getTargetContext().getSystemService("window")).getDefaultDisplay().getRealMetrics(metrics);
    float w=metrics.widthPixels,h=metrics.heightPixels,d=metrics.density,u=Math.min(58*d,h/5.2f),margin=18*d;
    float x=w*.5f+8*d+.75f*u,y=h-margin-.4f*u;
    UiAutomation ui=getInstrumentation().getUiAutomation();
    for(int i=0;i<3;i++) {startForFrames(ui,x,y);waitFrames(181);}
    waitFrames(240);
    File out=new File(getInstrumentation().getTargetContext().getFilesDir(),"campaign-start.png");
    try(FileOutputStream stream=new FileOutputStream(out)) {ui.takeScreenshot().compress(android.graphics.Bitmap.CompressFormat.PNG,100,stream);}
  }
}
