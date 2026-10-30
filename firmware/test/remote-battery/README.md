# Banc de test de l'alerte pile des télécommandes

Teste `remote_battery.c` (décision « pile faible » à partir du bit VLOW des trames),
compilé **tel quel**, sur PC. Le module est **pur** : un simple `gcc`/`clang` suffit.

```bash
cc -std=gnu11 -Wall -Wextra -fsanitize=address -g \
   -I main -I ../../main main/test_main.c ../../main/remote_battery.c -o remote_battery_host_test
./remote_battery_host_test
```

Le programme rend 0 si tout passe, 1 sinon.
