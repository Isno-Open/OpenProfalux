# Banc de test des profils radio

Teste `radio_profile.c` (profils de fréquence Profalux / Eveno / personnalisé, plage
autorisée, mot de fréquence du CC1101), compilé **tel quel**, sur PC. Le module est
**pur** (aucune dépendance ESP-IDF) : un simple `gcc`/`clang` suffit.

## Lancer

Depuis ce dossier :

```bash
cc -std=gnu11 -Wall -Wextra -fsanitize=address -g \
   -I main -I ../../main main/test_main.c ../../main/radio_profile.c -o radio_profile_host_test
./radio_profile_host_test
```

Le programme rend 0 si tout passe, 1 sinon. Les mots de fréquence attendus sont ceux
qu'écrivait `cc1101.c` en dur avant les profils (Profalux `21 66 A5`) et celui mesuré
sur une installation Eveno (`21 65 B6`).
