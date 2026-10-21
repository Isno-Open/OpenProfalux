# Banc de test des envois venus d'un autre site

Teste `req_guard.c`, compilé **tel quel**, sur PC. Sans mot de passe (le défaut), une page piégée ouverte
sur un PC de la maison pouvait faire envoyer au navigateur un POST vers le boîtier (CSRF). Le boîtier
refuse maintenant ces envois-là, et seulement eux : l'interface, une iframe dans Home Assistant, Home
Assistant en REST, curl et les scripts ne changent pas.

```bash
cc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address -g \
   -I main -I ../../main main/test_main.c ../../main/req_guard.c -o req_guard_host_test
./req_guard_host_test
```

| Test | Attendu |
|---|---|
| T0-T1 | sans en-tête (Home Assistant en REST, curl, scripts) : accepté |
| T2-T7 | `Sec-Fetch-Site` same-origin (interface, iframe dans Home Assistant, proxy), same-site, none : accepté |
| T8-T10b | `Sec-Fetch-Site` cross-site : refusé, même si l'origine ressemble, quelle que soit la casse |
| T11-T16 | navigateur ancien, même hôte : accepté (port, casse, point final, IPv6, 192.168.4.1) |
| T17-T23 | navigateur ancien : autre hôte, hôte qui commence pareil, `null`, sans `Host`, origine vide ou mal formée : refusé |

Le banc détecte chaque règle cassée exprès (9 mutations) : cross-site ignoré, sans en-tête refusé, port
gardé, casse comptée, `null` accepté, préfixe accepté, point final gardé, espaces gardés, `Host` absent
accepté.
