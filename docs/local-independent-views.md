# Local independent screens

In **Mods → Characters → Co-op**, set **Local co-op display** to
**Side by side (Experimental)**, then start or restart the game. Assign the
two players' controls as usual. Co-op still uses your original X3 USA ROM for
Zero. **Unified** remains the default local layout.

Each player gets their own camera and health/weapon HUD in the same window.
Without widescreen, each pane is **5:4**, making a **5:2** combined canvas.
With widescreen enabled, its ratio applies to each pane: 16:9 produces
16:9 + 16:9, for example. Adaptive uses each half of the drawable window and
letterboxes when the window is too narrow or exceeds the renderer's limits.
Each pane supports up to 1024 source pixels, for a 2048-pixel combined frame.
Local panes retain 5:4 base pixel proportions; Settings → Display Aspect
continues to control Unified views.

Players can separate using the existing independent-camera simulation rules.
The world, enemies, audio, pickups and stage progression still update once.
Story events and doors retain the existing co-op ownership and reunion rules;
fallen or withdrawn players follow the surviving player's camera.
Both cameras retain the native tracking dead zones and gradual room pans,
including the Highway camera transition after Bee Blader is defeated.

Native pause menus keep their existing ownership: either player opens their
own inventory, which appears in both panes while the world pauses. Shared
menus and scripted transitions also appear in both panes. When a private
frame is unavailable, both panes display the current shared frame.

**Netplay cameras** remains a separate online setting. Selecting a local
layout does not create a network session or change online camera behavior.
The local mode reuses the experimental independent-camera path, so its
existing stage-specific camera/art limitations also apply here.

Tracking: `beads-8wg.1.155`; shared presentation support: `beads-8wg.2.191`.
