import configparser
import runpy
import tempfile
import unittest
from pathlib import Path


set_ini = runpy.run_path(str(Path(__file__).resolve().parents[1] / ".local/bin/sync-cursor"))["set_ini"]


class CursorSettingsTests(unittest.TestCase):
    def rewrite(self, original, section, values):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.ini"
            if original is not None:
                path.write_text(original)
            set_ini(path, values, section=section)
            settings = configparser.ConfigParser()
            settings.optionxform = str
            settings.read(path)
            return settings

    def test_mouse_cursor_is_not_written_into_touchpad_group(self):
        settings = self.rewrite(
            "[Mouse]\n[Libinput][1267][12474][Touchpad]\nNaturalScroll=true\n",
            "Mouse",
            {"cursorTheme": "Bibata-Modern-Classic", "cursorSize": "24"},
        )
        self.assertEqual(settings["Mouse"].get("cursorTheme"), "Bibata-Modern-Classic")
        self.assertEqual(settings["Mouse"].get("cursorSize"), "24")
        self.assertEqual(dict(settings["Libinput][1267][12474][Touchpad"]), {"NaturalScroll": "true"})

    def test_updates_only_the_requested_section(self):
        settings = self.rewrite(
            "[Settings]\ngtk-cursor-theme-name=Premium\n"
            "[Other]\ngtk-cursor-theme-name=OtherTheme\n",
            "Settings",
            {"gtk-cursor-theme-name": "Bibata-Modern-Classic"},
        )
        self.assertEqual(settings["Settings"]["gtk-cursor-theme-name"], "Bibata-Modern-Classic")
        self.assertEqual(settings["Other"]["gtk-cursor-theme-name"], "OtherTheme")

    def test_creates_missing_target_section(self):
        for original in (None, "[Other]\nNaturalScroll=true\n"):
            with self.subTest(original=original):
                settings = self.rewrite(original, "Mouse", {"cursorTheme": "Bibata-Modern-Classic"})
                self.assertEqual(settings["Mouse"].get("cursorTheme"), "Bibata-Modern-Classic")
                if original is not None:
                    self.assertEqual(dict(settings["Other"]), {"NaturalScroll": "true"})


if __name__ == "__main__":
    unittest.main()
