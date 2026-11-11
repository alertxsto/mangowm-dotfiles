import configparser
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]


class BrowserInstallationTests(unittest.TestCase):
    def install(self, home, browser=None, check=True):
        command = [
            str(REPO / "install.sh"), "--target-home", str(home),
            "--skip-packages", "--skip-services", "--skip-build", "--yes",
        ]
        if browser is not None:
            command.extend(["--browser", browser])
        return subprocess.run(command, capture_output=True, text=True, check=check)

    def browser_default(self, home):
        settings = configparser.ConfigParser(interpolation=None)
        settings.read(home / ".config/mimeapps.list")
        return settings["Default Applications"]["x-scheme-handler/https"].rstrip(";")

    def test_browser_selection_sets_defaults_without_unselected_origin_launcher(self):
        for browser, desktop in (
            ("brave-origin", "brave-origin.desktop"),
            ("brave", "brave-browser.desktop"),
            ("chromium", "chromium.desktop"),
            ("firefox", "firefox.desktop"),
        ):
            with self.subTest(browser=browser), tempfile.TemporaryDirectory() as directory:
                home = Path(directory)
                self.install(home, browser)
                self.assertEqual(self.browser_default(home), desktop)
                self.assertEqual(
                    (home / ".local/share/applications/brave-origin.desktop").exists(),
                    browser == "brave-origin",
                )
                kde = configparser.ConfigParser(interpolation=None)
                kde.read(home / ".config/kdeglobals")
                self.assertEqual(kde["General"]["BrowserApplication"], desktop)

    def test_selected_browser_survives_startup_sync_and_reinstallation(self):
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory)
            self.install(home, "firefox")
            subprocess.run(
                [str(home / ".local/bin/sync-default-apps")], check=True,
                env=dict(os.environ, HOME=str(home), XDG_CONFIG_HOME=str(home / ".config"),
                         XDG_DATA_HOME=str(home / ".local/share")),
                capture_output=True, text=True,
            )
            self.assertEqual(self.browser_default(home), "firefox.desktop")
            self.install(home)
            self.assertEqual(self.browser_default(home), "firefox.desktop")

    def test_chromium_keyring_flags_preserve_other_options_and_are_idempotent(self):
        for browser, filename in (
            ("brave-origin", "brave-origin-flags.conf"),
            ("chromium", "chromium-flags.conf"),
        ):
            with self.subTest(browser=browser), tempfile.TemporaryDirectory() as directory:
                home = Path(directory)
                flags = home / ".config" / filename
                flags.parent.mkdir()
                flags.write_text("--disable-features=Dav1dVideoDecoder\n--password-store=basic\n")
                self.install(home, browser)
                configured = flags.read_text()
                self.assertIn("--disable-features=Dav1dVideoDecoder", configured.splitlines())
                stores = [line for line in configured.splitlines() if line.startswith("--password-store=")]
                self.assertEqual(stores, ["--password-store=kwallet6"])
                self.install(home)
                self.assertEqual(flags.read_text(), configured)

    def test_switching_browser_preserves_unrelated_mime_defaults(self):
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory)
            self.install(home, "brave-origin")
            mimeapps = home / ".config/mimeapps.list"
            with mimeapps.open("a") as stream:
                stream.write("\n[Added Associations]\napplication/pdf=custom-pdf.desktop;\n")
            self.install(home, "firefox")
            self.assertEqual(self.browser_default(home), "firefox.desktop")
            self.assertFalse((home / ".local/share/applications/brave-origin.desktop").exists())
            settings = configparser.ConfigParser(interpolation=None)
            settings.read(mimeapps)
            self.assertEqual(settings["Added Associations"]["application/pdf"], "custom-pdf.desktop;")

    def test_unknown_browser_is_rejected_before_target_creation(self):
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory) / "not-created"
            result = self.install(home, "unknown-browser", check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unsupported browser", result.stderr)
            self.assertFalse(home.exists())


if __name__ == "__main__":
    unittest.main()
