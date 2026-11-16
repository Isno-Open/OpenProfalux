# Banc du garde-fou réseau

`net_watch.c` décide, sonde après sonde de la passerelle, quand relancer le Wi-Fi et quand
redémarrer le boîtier. Unité pure : un simple `cc` suffit, sans ESP-IDF.

```bash
cc -std=gnu11 -Wall -Wextra -fsanitize=address -g -I ../../main main/test_main.c ../../main/net_watch.c -o net_watch_host_test
./net_watch_host_test
```
