#!/usr/bin/env python3
"""Refuse toute boite du navigateur (alert, confirm, prompt) dans l'interface locale.

Pourquoi : decide par Olivier le 2026-11-16, ces boites sortent du style de l'interface et
certains navigateurs integres les bloquent sans un mot (confirm rend alors false : le bouton
« Oublier » ne faisait plus rien). L'interface confirme par sa propre fenetre, ask() dans app.js.

Usage : check_web_dialogs.py [dossier]   (par defaut firmware/main/web)
"""
import pathlib
import re
import sys

# Bibliotheques tierces recopiees telles quelles : on ne les reecrit pas.
VENDORED = {"qrcode.js"}
DIALOG_CALL = re.compile(r"\b(?:window\.)?(alert|confirm|prompt)\s*\(")

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "firmware/main/web")
found = []
for f in sorted(list(root.glob("*.js")) + list(root.glob("*.html"))):
    if f.name in VENDORED:
        continue
    for n, line in enumerate(f.read_text(encoding="utf-8").splitlines(), 1):
        if DIALOG_CALL.search(line):
            found.append(f"{f}:{n}: {line.strip()}")
if found:
    print("Boite du navigateur interdite, utiliser ask() ou toast() :")
    print("\n".join(found))
    sys.exit(1)
print(f"aucune boite du navigateur dans {root}")
