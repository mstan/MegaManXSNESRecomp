#!/usr/bin/env python3
"""Check the required shipped Saber Zero asset layout."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
ASSET_ROOT = ROOT / "assets" / "saber-zero"
REQUIRED_ASSETS = (
    ASSET_ROOT / "sprites" / "SaberJump.png",
    ASSET_ROOT / "sprites" / "SaberLand.png",
    ASSET_ROOT / "sprites" / "Saber_1.png",
    ASSET_ROOT / "sprites" / "Saber_2.png",
    ASSET_ROOT / "sprites" / "Saber_3.png",
    ASSET_ROOT / "sprites" / "Saber_Dash.png",
    ASSET_ROOT / "sprites" / "Saber_Wall.png",
    ASSET_ROOT / "sprites" / "ride_zero.png",
    ASSET_ROOT / "sfx" / "saber_1.ogg",
    ASSET_ROOT / "sfx" / "saber_2.ogg",
    ASSET_ROOT / "sfx" / "saber_3.ogg",
)


class SaberZeroAssetLayoutTests(unittest.TestCase):
    def test_required_assets_are_under_tracked_asset_root(self) -> None:
        self.assertTrue(ASSET_ROOT.is_dir(), ASSET_ROOT)
        for path in REQUIRED_ASSETS:
            with self.subTest(path=path):
                self.assertTrue(path.is_relative_to(ASSET_ROOT), path)
                self.assertTrue(path.is_file(), path)

    def test_credits_exists_and_records_noncommercial_permission(self) -> None:
        credits = ASSET_ROOT / "CREDITS.md"
        self.assertTrue(credits.is_file(), credits)
        text = credits.read_text(encoding="utf-8")
        self.assertIn("Zashiko Mod", text)
        self.assertIn("non-commercial", text)
        self.assertIn("not monetized", text)
        self.assertIn("ROMs are never included", text)


if __name__ == "__main__":
    unittest.main()
