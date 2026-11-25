#!/usr/bin/env python3
"""Engendre la table des profils du firmware depuis firmware/main/web/profiles.json (issue #134).

    python3 tools/gen_profiles.py <profiles.json> <sortie.c>

profiles.json est la source unique des types, des boutons et des profils. L'interface le lit sur le
boitier ; le firmware, lui, a besoin de connaitre ses profils pour qu'un appareil suive le sien. Plutot
que de recopier les valeurs dans le C, la construction les engendre ici, dans le dossier de construction
(rien d'engendre n'est commis), comme board_pins.h depuis boards/<carte>.json.

Le script REFUSE un catalogue incoherent, avec le fichier, le profil et la raison : une erreur de profil
casse la construction au lieu de partir sur un boitier. Il ne decide pas des moteurs connus : le moteur
et la methode d'ajout sont ecrits tels quels, en chaines, et c'est profile_table.c qui les reconnait (le
banc firmware/test/profile-table verifie que chaque profil du vrai catalogue y est reconnu).
"""
import json
import sys

# Taille d'un identifiant dans la fiche d'un appareil (SH_ID_LEN de shutters.h, NUL compris).
ID_MAX = 23
# Commandes d'un bouton, dans l'ordre de profile_cmd_t (profile_table.h) : indices des tableaux par commande.
CMD_SLOTS = ("up", "stop", "down", "step")
# Commandes qui ont un emplacement de code de bouton dans profile_def_t (code_up, code_stop, code_down).
CODE_SLOTS = ("up", "stop", "down")
# Source d'une trame de bouton : la capture de la telecommande d'origine (frames.<bouton>.source).
FRAME_SOURCE_CAPTURE = "capture"
# Le bouton pas a pas : son comportement et sa commande vont ensemble (types[].buttons[]).
STEP_BEHAVIOR = "step"
STEP_CMD = "step"
# Actions d'un cycle pas a pas (frames.<bouton>.cycle) et leur constante C (step_cycle.h, step_act_t).
STEP_ACTIONS = {"open": "STEP_ACT_OPEN", "stop": "STEP_ACT_STOP", "close": "STEP_ACT_CLOSE"}
STEP_CYCLE_MAX = 4   # STEP_CYCLE_MAX de step_cycle.h
STEP_OPEN = "open"   # l'action que tout cycle contient : un portail qu'on ne peut pas ouvrir n'a rien a piloter
# Champs d'une trame captee, en plus de sa source.
CAPTURE_FIELDS = {"source", "repeats", "gap_us", "cycle", "press_gap_ms"}


class CatalogError(Exception):
    pass


def require(cond, where, reason):
    if not cond:
        raise CatalogError(f"{where} : {reason}")


def c_string(s):
    """Chaine C sure : seuls les caracteres imprimables ASCII passent tels quels."""
    out = []
    for ch in s.encode("utf-8"):
        if ch in (0x22, 0x5C):
            out.append("\\" + chr(ch))
        elif 0x20 <= ch < 0x7F:
            out.append(chr(ch))
        else:
            out.append(f"\\{ch:03o}")
    return '"' + "".join(out) + '"'


def check(doc):
    """Verifie le catalogue ; rend la liste des profils a ecrire."""
    require(isinstance(doc, dict), "profiles.json", "objet attendu")
    types = doc.get("types")
    require(isinstance(types, dict) and types, "profiles.json", "types requis")
    codes = {}
    for key, t in types.items():
        where = f"type {key}"
        require(isinstance(t, dict), where, "objet attendu")
        require(isinstance(t.get("code"), int) and 0 <= t["code"] <= 255, where, "code entier de 0 a 255 requis")
        require(t["code"] not in codes, where, f"code {t['code']} deja pris par le type {codes.get(t['code'])}")
        codes[t["code"]] = key
        require(isinstance(t.get("ha_device_class"), str) and t["ha_device_class"], where, "ha_device_class requis (classe de l'appareil dans Home Assistant)")
        require(isinstance(t.get("positional", False), bool), where, "positional : booleen")
        buttons = t.get("buttons")
        require(isinstance(buttons, list) and buttons, where, "au moins un bouton requis")
        seen = set()
        for i, b in enumerate(buttons):
            bw = f"{where}, bouton {i + 1}"
            require(isinstance(b, dict) and isinstance(b.get("id"), str) and b["id"], bw, "id requis")
            require(b["id"] not in seen, bw, f"bouton {b['id']} en double")
            seen.add(b["id"])
            for field in ("cmd", "behavior", "label"):
                require(isinstance(b.get(field), str) and b[field], f"{bw} ({b['id']})", f"{field} requis")
            require(b["cmd"] in CMD_SLOTS, f"{bw} ({b['id']})", f"cmd {b['cmd']} inconnue ({', '.join(CMD_SLOTS)})")
            require((b["behavior"] == STEP_BEHAVIOR) == (b["cmd"] == STEP_CMD), f"{bw} ({b['id']})",
                    f"le comportement {STEP_BEHAVIOR} et la commande {STEP_CMD} vont ensemble")
            pos = b.get("position")
            require(isinstance(pos, dict) and isinstance(pos.get("row"), int) and isinstance(pos.get("col"), int),
                    f"{bw} ({b['id']})", "position { row, col } entiere requise")

    profiles = doc.get("profiles")
    require(isinstance(profiles, list) and profiles, "profiles.json", "profiles requis")
    ids = set()
    seen_keys = {}
    out = []
    for i, p in enumerate(profiles):
        where = f"profil {i + 1}"
        require(isinstance(p, dict), where, "objet attendu")
        pid = p.get("id")
        require(isinstance(pid, str) and pid, where, "id requis")
        where = f"profil {pid}"
        require(len(pid.encode("utf-8")) <= ID_MAX, where, f"id trop long ({ID_MAX} octets au plus, il vit dans la fiche de l'appareil)")
        require(all(c.isascii() and (c.isalnum() or c == "-") for c in pid), where, "id en lettres ASCII, chiffres et tirets")
        require(pid not in ids, where, "id en double")
        ids.add(pid)
        for field in ("name", "brand", "add", "engine"):
            require(isinstance(p.get(field), str) and p[field], where, f"{field} requis")
        require(len(p["brand"].encode("utf-8")) <= ID_MAX, where, f"brand trop longue ({ID_MAX} octets au plus, elle vit dans la fiche de l'appareil)")
        require(p.get("type") in types, where, f"type « {p.get('type')} » inconnu")
        radio = p.get("radio")
        require(isinstance(radio, dict) and isinstance(radio.get("band"), str) and radio["band"], where, "radio.band requis")
        freq = radio.get("freq_khz", 0)
        require(isinstance(freq, int) and freq >= 0, where, "radio.freq_khz entier positif")
        # Le firmware retrouve le profil d'une fiche par sa marque et sa methode (profile_resolve) : deux profils
        # d'une meme marque, de meme methode et de meme moteur seraient indiscernables.
        key = (p["brand"], p["add"], p["engine"])
        require(key not in seen_keys, where, f"meme marque, methode et moteur que le profil {seen_keys.get(key)} : indiscernables pour le firmware")
        seen_keys[key] = pid
        # Trames par bouton : chaque cle est un bouton du type, chaque valeur une capture ou un code de bouton.
        # Le code se range sous la commande du bouton (up, stop, down) : c'est elle que le firmware emet.
        def count(obj, key, lo, hi, label):
            v = obj.get(key, 0)
            require(isinstance(v, int) and (v == 0 or lo <= v <= hi), where, f"{label} : entier de {lo} a {hi}")
            return v
        buttons = {b["id"]: b for b in types[p["type"]]["buttons"]}
        codes, repeats, gaps, step = {}, {}, {}, None
        frames = p.get("frames", {})
        require(isinstance(frames, dict), where, "frames : objet attendu")
        for bid, f in frames.items():
            require(bid in buttons, where, f"frames.{bid} : le type {p['type']} n'a pas ce bouton")
            cmd, behavior = buttons[bid]["cmd"], buttons[bid]["behavior"]
            require(isinstance(f, dict), where, f"frames.{bid} : objet attendu")
            if "code" in f:
                require(len(f) == 1, where, f"frames.{bid} : une trame de code n'a que {{ \"code\": n }}")
                require(isinstance(f["code"], int) and 0 <= f["code"] <= 15, where, f"frames.{bid}.code : entier de 0 a 15")
                require(cmd in CODE_SLOTS, where, f"frames.{bid} : la commande {cmd} n'a pas d'emplacement de code")
                codes[cmd] = f["code"]
                continue
            require(f.get("source") == FRAME_SOURCE_CAPTURE and set(f) <= CAPTURE_FIELDS, where,
                    f"frames.{bid} : {{ \"source\": \"{FRAME_SOURCE_CAPTURE}\" }} (et repeats, gap_us, cycle, press_gap_ms) ou {{ \"code\": n }} attendu")
            # Rejeu : repetitions et silence de CE bouton, ensemble ou pas du tout.
            require(("repeats" in f) == ("gap_us" in f), where, f"frames.{bid} : repeats et gap_us vont ensemble")
            if "repeats" in f:
                repeats[cmd] = count(f, "repeats", 1, 50, f"frames.{bid}.repeats")
                gaps[cmd] = count(f, "gap_us", 1000, 100000, f"frames.{bid}.gap_us")
                require(repeats[cmd] and gaps[cmd], where, f"frames.{bid} : repeats et gap_us non nuls")
            # Pas a pas : le cycle de la carte du moteur, sur le bouton pas a pas et lui seul.
            if behavior == STEP_BEHAVIOR:
                require(step is None, where, "un seul bouton pas a pas par profil")
                cycle = f.get("cycle")
                require(isinstance(cycle, list) and 1 <= len(cycle) <= STEP_CYCLE_MAX and all(a in STEP_ACTIONS for a in cycle),
                        where, f"frames.{bid}.cycle : 1 a {STEP_CYCLE_MAX} actions parmi {', '.join(STEP_ACTIONS)}")
                require(STEP_OPEN in cycle, where, f"frames.{bid}.cycle : un appui au moins doit ouvrir")
                gap = count(f, "press_gap_ms", 100, 10000, f"frames.{bid}.press_gap_ms")
                require(gap, where, f"frames.{bid}.press_gap_ms requis (delai entre deux appuis)")
                step = {"cycle": cycle, "press_gap_ms": gap}
            else:
                require("cycle" not in f and "press_gap_ms" not in f, where,
                        f"frames.{bid} : cycle et press_gap_ms sont ceux d'un bouton {STEP_BEHAVIOR}")
        tx = p.get("tx", {})
        require(isinstance(tx, dict), where, "tx : objet attendu")
        require("repeats" not in tx and "gap_us" not in tx, where, "tx.repeats et tx.gap_us : par bouton, dans frames.<bouton>")
        enrol = p.get("enrol")
        require(enrol is None or isinstance(enrol, dict), where, "enrol : objet attendu")
        if enrol is not None:
            for key in ("code", "frames_per_burst", "bursts", "counter_start"):
                require(key in enrol, where, f"enrol.{key} requis")
        out.append({
            "id": pid, "brand": p["brand"], "type_code": types[p["type"]]["code"],
            "add": p["add"], "engine": p["engine"], "band": radio["band"], "freq_khz": freq,
            "frames_per_press": count(tx, "frames_per_press", 1, 50, "tx.frames_per_press"),
            "codes": codes, "repeats": repeats, "gaps": gaps, "step": step,
            "enrol": None if enrol is None else {
                "code": count(enrol, "code", 0, 15, "enrol.code"),
                "frames_per_burst": count(enrol, "frames_per_burst", 1, 50, "enrol.frames_per_burst"),
                "bursts": count(enrol, "bursts", 1, 50, "enrol.bursts"),
                "counter_start": count(enrol, "counter_start", 0, 65535, "enrol.counter_start"),
            },
        })
    return out


def render_types(types):
    lines = ["const type_def_t TYPE_TABLE[] = {"]
    for key, t in types.items():
        lines.append(f"    {{ .key = {c_string(key)}, .code = {t['code']}, .positional = {'true' if t.get('positional') else 'false'}, "
                     f".ha_device_class = {c_string(t['ha_device_class'])} }},")
    lines += ["};", f"const int TYPE_TABLE_COUNT = {len(types)};", ""]
    return lines


def render(profiles, source, types):
    lines = [
        f"/* ENGENDRE par tools/gen_profiles.py depuis {source} : ne pas modifier, ne pas commettre. */",
        '#include "profile_table.h"',
        "",
        "const profile_def_t PROFILE_TABLE[] = {",
    ]
    for p in profiles:
        code = lambda cmd: p["codes"].get(cmd, -1)
        e = p["enrol"]
        enrol = (f".has_enrol = true, .enrol_code = {e['code']}, .enrol_frames_per_burst = {e['frames_per_burst']}, "
                 f".enrol_bursts = {e['bursts']}, .enrol_counter_start = {e['counter_start']}") if e else ".has_enrol = false"
        per_cmd = lambda d: "{ " + ", ".join(str(d.get(c, 0)) for c in CMD_SLOTS) + " }"
        st = p["step"]
        step = (f".step = {{ .len = {len(st['cycle'])}, .act = {{ {', '.join(STEP_ACTIONS[a] for a in st['cycle'])} }} }}, "
                f".step_press_gap_ms = {st['press_gap_ms']}") if st else ".step = { .len = 0 }"
        lines.append(
            f"    {{ .id = {c_string(p['id'])}, .brand = {c_string(p['brand'])}, .type_code = {p['type_code']}, "
            f".add = {c_string(p['add'])}, .engine = {c_string(p['engine'])}, .band = {c_string(p['band'])}, "
            f".freq_khz = {p['freq_khz']}, .frames_per_press = {p['frames_per_press']}, "
            f".code_up = {code('up')}, .code_stop = {code('stop')}, .code_down = {code('down')}, "
            f".repeats = {per_cmd(p['repeats'])}, .gap_us = {per_cmd(p['gaps'])}, {step}, {enrol} }},"
        )
    lines += ["};", f"const int PROFILE_TABLE_COUNT = {len(profiles)};", ""]
    lines += render_types(types)
    return "\n".join(lines)


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    src, dst = argv[1], argv[2]
    try:
        with open(src, encoding="utf-8") as f:
            doc = json.load(f)
        profiles = check(doc)
    except (OSError, json.JSONDecodeError, CatalogError) as e:
        print(f"gen_profiles.py : {src} refuse : {e}", file=sys.stderr)
        return 1
    text = render(profiles, "firmware/main/web/profiles.json", doc["types"])
    try:
        with open(dst, encoding="utf-8") as f:
            if f.read() == text:
                return 0   # inchange : ne pas relancer la compilation
    except OSError:
        pass
    with open(dst, "w", encoding="utf-8") as f:
        f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
