# Banc d'essai de la couche liaison io

Teste `io_framer.c` (encapsulation UART à l'émission) et `io_unframer.c` (désassemblage à
la réception), compilés **tels quels**, sur PC. Les deux modules sont **purs** (aucune
dépendance ESP-IDF) : un simple `cc` suffit.

## Pourquoi ce banc existe

Au 2026-10-28, après **huit versions d'essai** avec le testeur (issue #87), aucun essai ne
couvrait la couche liaison io. Et la carte ne journalisait pas ce qu'elle **émet** : seule
la réception produit une ligne « Trame io captee ». On a donc toujours comparé notre
*intention* au trafic de la box de copie, jamais notre *émission* à la sienne.

Les trames de référence ne sont pas inventées pour l'essai : ce sont de **vraies trames
captées en l'air** chez le testeur (journaux 6, 7 et 8 de l'issue #87, plus la capture
d'enrôlement la box de copie du commentaire #20). C'est ce que le moteur reçoit et accepte.

## Lancer

Depuis ce dossier :

```bash
cc -std=gnu11 -Wall -Wextra -fsanitize=address -g \
   -I main -I ../../main/io \
   main/test_main.c ../../main/io/io_framer.c ../../main/io/io_unframer.c -o io_framing_host_test
./io_framing_host_test
```

Le programme rend 0 si tout passe, 1 sinon. 24 vérifications.

## Ce qu'il contrôle

| Contrôle | Ce qu'il attrape |
|---|---|
| règle de longueur `1 + (octet0 & 0x1F) + 2` | un en-tête qui annonce une taille fausse |
| bits hauts de l'octet 0 à `0xE0` | ordre ou protocole 1W non posés |
| **octet de résidu redérivé du flux** | un résidu qui ne correspond pas au mot de synchronisation |
| aller-retour encapsulation puis désassemblage | start, stop, ordre des bits, saut après le sync |

## Témoins de mutation

Mesurés le 2026-10-28, chaque mutation doit faire **rougir** le banc :

| Mutation | Résultat |
|---|---|
| résidu `0x99` → `0x98` | 1 échec |
| bit de stop `1` → `0` | 7 échecs |
| bits de donnée LSB d'abord → MSB d'abord | 7 échecs |
| bit de start `0` → `1` | 7 échecs |
| désassembleur : saut de `8` → `0` bits | 7 échecs |

⚠️ **Le premier contrôle a été ajouté parce qu'il manquait.** Sans lui, changer l'octet de
résidu laissait les 21 autres vérifications **vertes** : le désassembleur jette ses huit
premiers bits sans les lire, donc l'aller-retour ne peut pas voir l'erreur, alors qu'un vrai
récepteur, lui, décalerait toute la trame. Un contrôle qui ne peut pas échouer est pire
qu'un contrôle absent.

Le résidu est donc **redérivé** depuis le flux (préambule `0x55`, sync logique `0xFF 0x33`,
encapsulation UART, moins la fenêtre de 16 bits que le CC1101 consomme comme mot de
synchronisation), au lieu d'être recopié depuis `io_framer.c`.
