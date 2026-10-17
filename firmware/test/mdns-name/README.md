# Banc de test du nom mDNS

Teste `mdns_name.c`, compilé **tel quel**, sur PC (PR #58). Le nom mDNS du boîtier est
son nom choisi, nettoyé ; à défaut, le nom figé au premier démarrage : `openprofalux`
pour un boîtier déjà installé (adresse inchangée), `openprofalux-xxxx` pour un boîtier
neuf (2 derniers octets de la MAC du point d'accès, comme son SSID `OpenProfalux_XXXX`).

```bash
cc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address -g \
   -I main -I ../../main main/test_main.c ../../main/mdns_name.c -o mdns_name_host_test
./mdns_name_host_test
```

| Test | Attendu |
|---|---|
| T0-T2 | minuscules ; caractère invalide → tiret ; pas de tiret en tête, en queue ni double |
| T3 | nom tout invalide → vide |
| T4-T6 | tronqué à 32, sans tiret final, dans la taille du tampon |
| T7 | nom par défaut `openprofalux-0ac1` |
| T8 | noms par défaut reconnus (vide, `openprofalux`, `volet_test`) |
| T9-T10 | premier démarrage : boîtier installé → `openprofalux`, boîtier neuf → suffixe |
| T11 | noms par défaut reconnus sans tenir compte des majuscules (`OpenProfalux`) |
| T12 | identifiant du service `_openprofalux._tcp` : MAC **station** en 12 hexa minuscules |

Deux MAC distinctes, comme sur une carte (point d'accès = station + 1) : le nom par défaut et le
SSID prennent celle du **point d'accès** (`…0ac1`), l'identifiant `id` celle de la **station**
(`…0ac0`), qu'affiche la liste DHCP de la box. Le choix de la MAC lue est fait dans `main.c`.
