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


class Heritage(unittest.TestCase):
    def test_la_variante_433_garde_le_cablage_du_parent(self):
        for enfant, parent in (("kit-xiao-esp32s3-433", "kit-xiao-esp32s3-868"), ("kit-esp32-poe-433", "kit-esp32-poe-868")):
            e = gen_board.load_board(os.path.join(BOARDS, enfant + ".json"))
            p = gen_board.load_board(os.path.join(BOARDS, parent + ".json"))
            self.assertEqual(e["radios"][0]["pins"], p["radios"][0]["pins"], enfant)
            self.assertEqual(e["radios"][0]["band"], "433", enfant)
            self.assertEqual(e["mcu"], p["mcu"], enfant)
            self.assertEqual(e["id"], enfant)

    def test_le_kit_poe_garde_l_ethernet_de_la_carte(self):
        e = gen_board.load_board(os.path.join(BOARDS, "kit-esp32-poe-433.json"))
        base = json.load(open(os.path.join(BOARDS, "esp32-poe.json"), encoding="utf-8"))
        self.assertEqual(e["ethernet"], base["ethernet"])

    def test_fusion_des_listes_par_position(self):
        base = {"radios": [{"band": "868", "pins": {"cs": 1, "sck": 2}}, {"band": "433"}]}
        out = gen_board.merge_decl(base, {"radios": [{"pins": {"cs": 9}}]})
        self.assertEqual(out["radios"][0], {"band": "868", "pins": {"cs": 9, "sck": 2}})
        self.assertEqual(out["radios"][1], {"band": "433"})

    def test_une_liste_de_textes_est_remplacee(self):
        self.assertEqual(gen_board.merge_decl({"_comment": ["a", "b"]}, {"_comment": ["c"]}), {"_comment": ["c"]})

    def test_heritage_circulaire_refuse(self):
        with tempfile.TemporaryDirectory() as d:
            write(d, "a", {"id": "a", "extends": "b"})
            write(d, "b", {"id": "b", "extends": "a"})
            with self.assertRaises(SystemExit):
                gen_board.load_board(os.path.join(d, "a.json"))

    def test_enfant_sans_id_refuse(self):
        with tempfile.TemporaryDirectory() as d:
            write(d, "p", {"id": "p", "mcu": "esp32"})
            write(d, "e", {"extends": "p"})
            with self.assertRaises(SystemExit):
                gen_board.load_board(os.path.join(d, "e.json"))

    def test_parent_introuvable_refuse(self):
        with tempfile.TemporaryDirectory() as d:
            write(d, "e", {"id": "e", "extends": "absent"})
            with self.assertRaises(SystemExit):
                gen_board.load_board(os.path.join(d, "e.json"))


class LecteursPassentParLeChargeur(unittest.TestCase):
    """La mutation doit porter sur l'APPEL : un lecteur qui lirait le JSON brut verrait un
    enfant sans mcu ni Ethernet. On essaie donc les points d'entree reels."""

    def test_mcu_d_un_enfant_qui_ne_le_declare_pas(self):
        out = subprocess.run([sys.executable, GEN, "--mcu", os.path.join(BOARDS, "kit-esp32-poe-433.json")],
                             check=True, capture_output=True, text=True).stdout.strip()
        self.assertEqual(out, "esp32")

    def test_en_tete_d_un_enfant(self):
        with tempfile.TemporaryDirectory() as d:
            h = os.path.join(d, "board_pins.h")
            subprocess.run([sys.executable, GEN, os.path.join(BOARDS, "kit-esp32-poe-433.json"), h], check=True)
            t = open(h, encoding="utf-8").read()
        self.assertIn("#define CC1101_PIN_MISO      15", t)
        self.assertIn("#define CC1101_BAND_LOW_KHZ  433050", t)
        self.assertIn("#define BOARD_HAS_ETHERNET   1", t)

    def test_fragment_sdkconfig_d_un_enfant(self):
        with tempfile.TemporaryDirectory() as d:
            f = os.path.join(d, "frag")
            subprocess.run([sys.executable, GEN, "--sdkconfig", os.path.join(BOARDS, "kit-xiao-esp32s3-433.json"), f], check=True)
            self.assertIn("PSRAM presente", open(f, encoding="utf-8").read())


class ToutesLesCartes(unittest.TestCase):
    def test_chaque_carte_se_resout_et_tient_sa_bande(self):
        fichiers = sorted(glob.glob(os.path.join(BOARDS, "*.json")))
        self.assertTrue(fichiers)
        for f in fichiers:
            d = gen_board.load_board(f)
            nom = os.path.basename(f)[:-5]
            self.assertEqual(d["id"], nom, f)
            self.assertTrue(d.get("mcu"), f)
            bandes = {str(b["radio"]): b for b in d.get("bands_allowed_khz", [])}
            for r in d["radios"]:
                b = bandes.get(str(r["band"]))
                self.assertIsNotNone(b, "%s : radio %s sans plage" % (nom, r["band"]))
                self.assertTrue(b["low"] <= b["carrier_khz"] <= b["high"], "%s : porteuse hors bande" % nom)


if __name__ == "__main__":
    unittest.main()
