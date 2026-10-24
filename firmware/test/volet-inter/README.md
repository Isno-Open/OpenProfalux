# Banc de test de la position intermédiaire

Teste `volet_inter.c` (codes boutons de la position intermédiaire du moteur, et suivi de
position quand le moteur y part de lui-même), compilé **tel quel**, sur PC. Le module est
**pur** : un simple `gcc`/`clang` suffit.

```bash
cc -std=gnu11 -Wall -Wextra -fsanitize=address -g \
   -I main -I ../../main main/test_main.c ../../main/volet_inter.c -o volet_inter_host_test
./volet_inter_host_test
```

Le programme rend 0 si tout passe, 1 sinon.
