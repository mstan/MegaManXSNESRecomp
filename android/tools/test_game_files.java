package org.retroporting.megamanx;
import java.io.*;
import java.nio.file.*;
import java.util.zip.*;

class test_game_files {
  static void check(boolean good, String label) {
    if (!good)
      throw new AssertionError(label);
  }
  public static void main(String[] args) throws Exception {
    File root = Files.createTempDirectory("mmx-android-files-").toFile();
    try {
      GameFiles.seed(root);
      check(GameFiles.settings(root).get("Aspect").equals("adaptive"), "fresh Fit");
      check(GameFiles.settings(root).get("CrashReplay").equals("1"),
            "fresh recording");
      GameFiles.setting(root, "Aspect", "4:3");
      GameFiles.setting(root, "CrashReplay", "0");
      GameFiles.seed(root);
      check(GameFiles.settings(root).get("Aspect").equals("4:3"),
            "saved aspect preserved");
      check(GameFiles.settings(root).get("CrashReplay").equals("0"),
            "saved recording choice preserved");
      GameFiles.setting(root, "AndroidTouch", "0");
      GameFiles.setting(root, "BossRush", "0");
      check(GameFiles.settings(root).get("AndroidTouch").equals("0"),
            "touch independent of native ini");
      byte[] nativeSave = "[Android]\nAspect=adaptive\n".getBytes();
      GameFiles.replace(new File(root, "android-settings.ini"), nativeSave);
      check(GameFiles.settings(root).get("AndroidTouch").equals("0"),
            "touch survives native save");
      File rom = new File(root, "mmx.sfc");
      GameFiles.replace(rom, new byte[] {7, 8, 9});
      try {
        GameFiles.readRom(new ByteArrayInputStream(new byte[0x180000]));
        throw new AssertionError("bad ROM accepted");
      } catch (IOException expected) {
      }
      check(Files.readAllBytes(rom.toPath())[0] == 7,
            "failed import preserves prior ROM");
      if (args.length > 0) {
        byte[] data;
        try (InputStream in = new FileInputStream(args[0])) {
          data = GameFiles.readRom(in);
        }
        check(data.length == 0x180000, "valid private ROM");
        byte[] headered = new byte[data.length + 512];
        System.arraycopy(data, 0, headered, 512, data.length);
        check(GameFiles.readRom(new ByteArrayInputStream(headered)).length ==
                  data.length,
              "copier header");
      }
      ByteArrayOutputStream bytes = new ByteArrayOutputStream();
      try (ZipOutputStream out = new ZipOutputStream(bytes)) {
        out.putNextEntry(new ZipEntry("mods/../../escape"));
        out.write(1);
        out.closeEntry();
      }
      try {
        GameFiles.content(root, new ByteArrayInputStream(bytes.toByteArray()),
                          "test");
        throw new AssertionError("zip traversal accepted");
      } catch (IOException expected) {
      }
      check(!new File(root, "bundled-content-version").exists(),
            "failed content does not commit marker");
      System.out.println(
          "PASS: defaults, persistence, private ROM identity/header, atomic " +
          "replacement and archive containment");
    } finally {
      try (java.util.stream.Stream<Path> paths = Files.walk(root.toPath())) {
        for (Path p : paths.sorted(java.util.Comparator.reverseOrder())
                          .toArray(Path[] ::new))
          Files.delete(p);
      }
    }
  }
}
