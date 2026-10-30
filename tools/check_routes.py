#!/usr/bin/env python3
"""Verifie que les routes du serveur web tiennent dans leurs tables (firmware/main/web_ui.c).

Une route de trop n'est PAS enregistree : le boitier repond 404 et seul le journal de demarrage
le dit. Depuis #42, 49 routes protegees pour 48 places laissaient /api/central dehors (v0.2.12).
Ce script compte les reg(s, "...") et reg_open(s, "...") de web_ui.c, lit MAX_ROUTES et
MAX_OPEN_ROUTES, echoue si une table deborde et avertit quand il reste moins de 4 places.

    python3 tools/check_routes.py [chemin/vers/web_ui.c]
"""
import os
import re
import sys

MARGE = 4

here = os.path.dirname(os.path.abspath(__file__))
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "firmware", "main", "web_ui.c")
src = open(path, encoding="utf-8").read()


def define(nom):
    m = re.search(r"^#define\s+%s\s+(\d+)" % nom, src, re.M)
    if not m:
        sys.exit("%s introuvable dans %s" % (nom, path))
    return int(m.group(1))


tables = [
    ("protegees", len(re.findall(r'^\s+reg\(s, "', src, re.M)), define("MAX_ROUTES"), "MAX_ROUTES"),
    ("ouvertes", len(re.findall(r'^\s+reg_open\(s, "', src, re.M)), define("MAX_OPEN_ROUTES"), "MAX_OPEN_ROUTES"),
]
ko = False
for nom, n, places, cle in tables:
    print("routes %s : %d / %d (%s)" % (nom, n, places, cle))
    if n > places:
        print("::error::%d routes %s pour %d places : %d de trop, non enregistree(s) au demarrage "
              "(404). Relever %s dans web_ui.c." % (n, nom, places, n - places, cle))
        ko = True
    elif places - n < MARGE:
        print("::warning::plus que %d place(s) pour les routes %s : relever %s dans web_ui.c."
              % (places - n, nom, cle))
sys.exit(1 if ko else 0)
