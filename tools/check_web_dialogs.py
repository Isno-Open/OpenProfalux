#!/usr/bin/env python3
"""Refuse toute boite du navigateur (alert, confirm, prompt) dans l'interface locale.

Pourquoi : decide par Olivier le 2026-11-16, ces boites sortent du style de l'interface et
certains navigateurs integres les bloquent sans un mot (confirm rend alors false : le bouton
« Oublier » ne faisait plus rien). L'interface confirme dans la page, a cote du bouton (voir ui/).

Usage : check_web_dialogs.py [dossier...]   (par defaut ui/src et firmware/main/web)
"""
import pathlib
import re
import sys

DIALOG_CALL = re.compile(r"\b(?:window\.)?(alert|confirm|prompt)\s*\(")
SUFFIXES = {".js", ".ts", ".vue", ".html"}

roots = [pathlib.Path(a) for a in sys.argv[1:]] or [pathlib.Path("ui/src"), pathlib.Path("firmware/main/web")]
found, seen = [], 0
for root in roots:
    for f in sorted(p for p in root.rglob("*") if p.suffix in SUFFIXES and "node_modules" not in p.parts):
        seen += 1
        for n, line in enumerate(f.read_text(encoding="utf-8").splitlines(), 1):
            if DIALOG_CALL.search(line):
                found.append(f"{f}:{n}: {line.strip()}")
if found:
    print("Boite du navigateur interdite, confirmer dans la page ou toast() :")
    print("\n".join(found))
    sys.exit(1)
print(f"aucune boite du navigateur dans {' et '.join(map(str, roots))} ({seen} fichiers)")
