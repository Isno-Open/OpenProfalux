# Banc de test du compteur des télécommandes virtuelles

Teste `virt_counter.c`, compilé **tel quel**, sur PC. Une télécommande virtuelle (KeeLoq pour Profalux,
code roulant pour Somfy RTS) avance son compteur à chaque ordre, et le moteur retient le dernier reçu.
Restaurer une sauvegarde reprenait le compteur de la sauvegarde : s'il avait servi depuis, le boîtier
émettait des compteurs déjà vus, que le moteur ignore. On garde maintenant le plus avancé des deux, comme
la table d'enrôlement le fait déjà (`cnt_ahead` dans `pfx_enrol.c`), modulo 2^16.

```bash
cc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address -g \
   -I main -I ../../main main/test_main.c ../../main/virt_counter.c -o virt_counter_host_test
./virt_counter_host_test
```

| Test | Attendu |
|---|---|
| T0-T2 | un pas d'avance, un pas de retard, égal |
| T3-T4 | le passage à 0 après 65535 |
| T5-T6 | demi-tour moins un pas en avance, demi-tour exact non (même règle que `cnt_ahead`) |
| T7-T11 | à la restauration : sauvegarde plus ancienne, plus récente, égale, passage à 0 d'un côté ou de l'autre |

Le banc détecte chaque règle cassée exprès : comparaison sans le modulo, égal compté en avance, demi-tour
compté en avance, garde inversée.
