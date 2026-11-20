"""Essais du generateur de carte : heritage (« extends ») et coherence de boards/.

    python3 -m unittest tools/test_gen_board.py

Lance par la CI (job cartes), avant toute compilation.
"""
import glob
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BOARDS = os.path.join(ROOT, "boards")
GEN = os.path.join(HERE, "gen_board.py")
sys.path.insert(0, HERE)
import gen_board  # noqa: E402


def write(d, name, obj):
    with open(os.path.join(d, name + ".json"), "w", encoding="utf-8") as f:
        json.dump(obj, f)
    return os.path.join(d, name + ".json")


class Inheritance(unittest.TestCase):
    def test_433_variant_keeps_parent_wiring(self):
        for child, parent in (("kit-xiao-esp32s3-433", "kit-xiao-esp32s3-868"), ("kit-esp32-poe-433", "kit-esp32-poe-868")):
            e = gen_board.load_board(os.path.join(BOARDS, child + ".json"))
            p = gen_board.load_board(os.path.join(BOARDS, parent + ".json"))
            self.assertEqual(e["radios"][0]["pins"], p["radios"][0]["pins"], child)
            self.assertEqual(e["radios"][0]["band"], "433", child)
            self.assertEqual(e["mcu"], p["mcu"], child)
            self.assertEqual(e["id"], child)

    def test_poe_kit_keeps_board_ethernet(self):
        e = gen_board.load_board(os.path.join(BOARDS, "kit-esp32-poe-433.json"))
        base = json.load(open(os.path.join(BOARDS, "esp32-poe.json"), encoding="utf-8"))
        self.assertEqual(e["ethernet"], base["ethernet"])

    def test_lists_merge_by_position(self):
        base = {"radios": [{"band": "868", "pins": {"cs": 1, "sck": 2}}, {"band": "433"}]}
        out = gen_board.merge_decl(base, {"radios": [{"pins": {"cs": 9}}]})
        self.assertEqual(out["radios"][0], {"band": "868", "pins": {"cs": 9, "sck": 2}})
        self.assertEqual(out["radios"][1], {"band": "433"})

    def test_text_list_is_replaced(self):
        self.assertEqual(gen_board.merge_decl({"_comment": ["a", "b"]}, {"_comment": ["c"]}), {"_comment": ["c"]})

    def test_circular_inheritance_refused(self):
        with tempfile.TemporaryDirectory() as d:
            write(d, "a", {"id": "a", "extends": "b"})
            write(d, "b", {"id": "b", "extends": "a"})
            with self.assertRaises(SystemExit):
                gen_board.load_board(os.path.join(d, "a.json"))

    def test_child_without_id_refused(self):
        with tempfile.TemporaryDirectory() as d:
            write(d, "p", {"id": "p", "mcu": "esp32"})
            write(d, "e", {"extends": "p"})
            with self.assertRaises(SystemExit):
                gen_board.load_board(os.path.join(d, "e.json"))

    def test_missing_parent_refused(self):
        with tempfile.TemporaryDirectory() as d:
            write(d, "e", {"id": "e", "extends": "absent"})
            with self.assertRaises(SystemExit):
                gen_board.load_board(os.path.join(d, "e.json"))


class ReadersGoThroughLoader(unittest.TestCase):
    """La mutation doit porter sur l'APPEL : un lecteur qui lirait le JSON brut verrait un
    enfant sans mcu ni Ethernet. On essaie donc les points d'entree reels."""

    def test_mcu_of_child_that_does_not_declare_it(self):
        out = subprocess.run([sys.executable, GEN, "--mcu", os.path.join(BOARDS, "kit-esp32-poe-433.json")],
                             check=True, capture_output=True, text=True).stdout.strip()
        self.assertEqual(out, "esp32")

    def test_header_of_child(self):
        with tempfile.TemporaryDirectory() as d:
            h = os.path.join(d, "board_pins.h")
            subprocess.run([sys.executable, GEN, os.path.join(BOARDS, "kit-esp32-poe-433.json"), h], check=True)
            t = open(h, encoding="utf-8").read()
        self.assertIn("#define CC1101_PIN_MISO      15", t)
        self.assertIn("#define CC1101_BAND_LOW_KHZ  433050", t)
        self.assertIn("#define BOARD_HAS_ETHERNET   1", t)
        # La seule radio est en 433 : sa porteuse doit etre connue du firmware, sinon il
        # demarrerait sur la porteuse Profalux (868), hors bande (revue Codex, PR #136).
        self.assertIn("#define CC1101_CARRIER_KHZ   433920", t)

    def test_sdkconfig_fragment_of_child(self):
        with tempfile.TemporaryDirectory() as d:
            f = os.path.join(d, "frag")
            subprocess.run([sys.executable, GEN, "--sdkconfig", os.path.join(BOARDS, "kit-xiao-esp32s3-433.json"), f], check=True)
            self.assertIn("PSRAM presente", open(f, encoding="utf-8").read())


class AllBoards(unittest.TestCase):
    def test_every_board_resolves_and_keeps_its_band(self):
        files = sorted(glob.glob(os.path.join(BOARDS, "*.json")))
        self.assertTrue(files)
        for f in files:
            d = gen_board.load_board(f)
            name = os.path.basename(f)[:-5]
            self.assertEqual(d["id"], name, f)
            self.assertTrue(d.get("mcu"), f)
            bands = {str(b["radio"]): b for b in d.get("bands_allowed_khz", [])}
            for r in d["radios"]:
                b = bands.get(str(r["band"]))
                self.assertIsNotNone(b, "%s : radio %s sans plage" % (name, r["band"]))
                self.assertTrue(b["low"] <= b["carrier_khz"] <= b["high"], "%s : porteuse hors bande" % name)


if __name__ == "__main__":
    unittest.main()
