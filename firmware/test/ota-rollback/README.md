# Banc de test du retour automatique

Teste `ota_rollback.c`, compilé **tel quel**, sur PC. Avec `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`,
une mise à jour démarre **en essai** (PENDING_VERIFY) ; si elle redémarre avant d'être validée, le
bootloader l'abandonne (ABORTED) et repart sur la version précédente. L'unité dit ce que le
firmware en conclut. Elle est **pure** (aucune dépendance ESP-IDF), un simple `gcc`/`clang` suffit.

```bash
cc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address -g \
   -I main -I ../../main main/test_main.c ../../main/ota_rollback.c -o ota_rollback_host_test
./ota_rollback_host_test
```

| Test | Attendu |
|---|---|
| T0 | état illisible : rien n'est conclu |
| T1 | en essai au démarrage : retour automatique **actif** (seul un bootloader avec l'option met en essai) |
| T2 | démarrée mais restée NEW : **inactif** (bootloader d'avant l'option, que l'OTA ne remplace pas) |
| T3 | validée, écartée, jamais mise à jour : la dernière observation est gardée |
| T4 | redémarrage voulu ou nouvelle mise à jour : une version en essai est validée |
| T5 | après le bouton « Version précédente » (INVALID) : **jamais** revalidée, sinon le retour serait annulé |
| T6 | rien d'autre n'est validé |
| T7 | « en essai » affiché seulement pour PENDING_VERIFY |
| T8-T10 | dernière image écartée : automatique (ABORTED), manuelle (INVALID), aucune |

Les valeurs des états sont celles d'`esp_ota_img_states_t` : `ota.c` le vérifie à la compilation.
