package org.retroporting.megamanx;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.*;
import java.util.*;
import java.util.zip.*;

/**
 * File contracts shared by the launcher and native runner. No storage
 * permissions.
 */
final class GameFiles {
  static final String ROM_SHA =
      "b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32";
  static void replace(File dest, byte[] data) throws IOException {
    File stage = new File(dest.getPath() + ".new");
    try (FileOutputStream out = new FileOutputStream(stage)) {
      out.write(data);
      out.getFD().sync();
    }
    Files.move(stage.toPath(), dest.toPath(), StandardCopyOption.ATOMIC_MOVE,
               StandardCopyOption.REPLACE_EXISTING);
  }
  static byte[] readRom(InputStream source) throws Exception {
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    byte[] chunk = new byte[16384];
    int n;
    while ((n = source.read(chunk)) != -1) {
      if (out.size() + n > 0x180200)
        throw new IOException("Choose an unmodified Mega Man X (USA, Rev 1) ROM, not a " +
                              "ZIP or patched copy.");
      out.write(chunk, 0, n);
    }
    byte[] raw = out.toByteArray();
    if (raw.length == 0x180200)
      raw = Arrays.copyOfRange(raw, 512, raw.length);
    if (raw.length != 0x180000 ||
        !hex(MessageDigest.getInstance("SHA-256").digest(raw)).equals(ROM_SHA))
      throw new IOException("This ROM does not match Mega Man X (USA, Rev 1). Headered " +
                            ".smc and headerless .sfc dumps are supported.");
    return raw;
  }
  static String hex(byte[] data) {
    StringBuilder s = new StringBuilder();
    for (byte b : data)
      s.append(String.format(Locale.ROOT, "%02x", b & 255));
    return s.toString();
  }
  static boolean hasRom(File root) {
    try (InputStream in = new FileInputStream(new File(root, "mmx.sfc"))) {
      readRom(in);
      return true;
    } catch (Exception e) {
      return false;
    }
  }
  static LinkedHashMap<String, String> settings(File root) throws IOException {
    LinkedHashMap<String, String> result = new LinkedHashMap<>();
    File f = new File(root, "android-settings.ini");
    if (f.exists())
      for (String line :
           Files.readAllLines(f.toPath(), StandardCharsets.UTF_8)) {
        int eq = line.indexOf('=');
        if (eq > 0)
          result.put(line.substring(0, eq).trim(),
                     line.substring(eq + 1).trim());
      }
    File touch = new File(root, "android-touch");
    if (touch.exists())
      result.put("AndroidTouch", new String(Files.readAllBytes(touch.toPath()),
                                            StandardCharsets.UTF_8));
    return result;
  }
  static void setting(File root, String key, String value) throws IOException {
    if (key.equals("AndroidTouch")) {
      replace(new File(root, "android-touch"),
              value.getBytes(StandardCharsets.UTF_8));
      return;
    }
    LinkedHashMap<String, String> map = settings(root);
    map.put(key, value);
    map.remove("AndroidTouch");
    StringBuilder out = new StringBuilder("[Android]\n");
    for (Map.Entry<String, String> kv : map.entrySet())
      out.append(kv.getKey()).append('=').append(kv.getValue()).append('\n');
    replace(new File(root, "android-settings.ini"),
            out.toString().getBytes(StandardCharsets.UTF_8));
  }
  static void seed(File root) throws IOException {
    if (!new File(root, "android-settings.ini").exists()) {
      replace(new File(root, "android-settings.ini"),
              ("[Android]\nWidescreen=1\nAspect=adaptive\nCrashReplay=1\n")
                  .getBytes(StandardCharsets.UTF_8));
    }
    File config = new File(root, "config.ini");
    if (!config.exists())
      replace(config, ("[General]\nSkipLauncher=1\n[Graphics]\nFullscreen=1\nNewRenderer=1\nNoSpriteLimits=1\nOutputMethod=SDL\nDisplayAspect=4:3\n[Sound]\nEnableAudio=1\n")
                          .getBytes(StandardCharsets.UTF_8));
  }
  static void content(File root, InputStream archive, String version)
      throws IOException {
    File marker = new File(root, "bundled-content-version");
    if (marker.exists() &&
        new String(Files.readAllBytes(marker.toPath()), StandardCharsets.UTF_8)
            .equals(version))
      return;
    try (ZipInputStream zip = new ZipInputStream(archive)) {
      ZipEntry item;
      long total = 0;
      while ((item = zip.getNextEntry()) != null) {
        String name = item.getName();
        if (!(name.startsWith("mods/") || name.startsWith("assets/") ||
              name.startsWith("licenses/")))
          throw new IOException("Unexpected bundled content path");
        File dest = new File(root, name);
        if (!dest.getCanonicalPath().startsWith(root.getCanonicalPath() +
                                                File.separator))
          throw new IOException("Unsafe content path");
        if (item.isDirectory()) {
          if (!dest.isDirectory() && !dest.mkdirs())
            throw new IOException("Cannot create content directory");
          continue;
        }
        if (!dest.getParentFile().isDirectory() &&
            !dest.getParentFile().mkdirs())
          throw new IOException("Cannot create content directory");
        ByteArrayOutputStream data = new ByteArrayOutputStream();
        byte[] buf = new byte[32768];
        int n;
        while ((n = zip.read(buf)) != -1) {
          total += n;
          if (total > 512L * 1024 * 1024)
            throw new IOException("Bundled content too large");
          data.write(buf, 0, n);
        }
        // Retain edits to previously installed content and all custom packs.
        if (!dest.exists())
          replace(dest, data.toByteArray());
      }
    }
    replace(marker, version.getBytes(StandardCharsets.UTF_8));
  }
}
