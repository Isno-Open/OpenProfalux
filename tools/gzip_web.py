#!/usr/bin/env python3
"""Compresse l'interface web pour l'embarquer dans le firmware.

    python3 tools/gzip_web.py build/web firmware/main/web/index.html ...

Chaque fichier devient <dossier>/<nom>.gz (gzip niveau 9). Le firmware les envoie
tels quels avec Content-Encoding: gzip : ~113 Ko de sources tiennent en ~35 Ko de
flash, et chaque page est trois fois plus legere a charger. Appele par CMake a la
configuration, comme gen_board.py ; rien d'engendre n'est commis, les sources
web/ restent lisibles dans le depot.

Module gzip de Python et non l'outil gzip, absent sous Windows. mtime=0 : meme
fichier a chaque compilation (compilations reproductibles). Un .gz deja a jour
n'est pas reecrit, pour ne pas relancer l'edition de liens pour rien.
"""
import gzip
import os
import sys


def main(argv):
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    out_dir, sources = argv[1], argv[2:]
    os.makedirs(out_dir, exist_ok=True)
    for src in sources:
        with open(src, "rb") as f:
            data = gzip.compress(f.read(), compresslevel=9, mtime=0)
        dst = os.path.join(out_dir, os.path.basename(src) + ".gz")
        try:
            with open(dst, "rb") as f:
                if f.read() == data:
                    continue
        except FileNotFoundError:
            pass
        with open(dst, "wb") as f:
            f.write(data)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
