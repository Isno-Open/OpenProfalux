# Banc de test io 1W

Depuis la racine du depot :

```sh
make -C firmware/test/io-1w test
```

Compile le vrai code de trame, d'emission, d'appairage et de stockage NVS avec
`-Wall -Wextra -Werror`, AddressSanitizer et UndefinedBehaviorSanitizer. Sortie dans
`firmware/build-host-io-1w/` (ignore par git). Sous un bac a sable ptrace, ajouter
`ASAN_OPTIONS=detect_leaks=0` (detection des fuites non supportee, les autres controles restent).

Ce qui est couvert : la capture Velux (longueur, CRC), le vecteur d'identite de
`feat/io-pair-known-identity`, le compteur poids fort en tete (y compris dans l'entree du HMAC),
le bit LPM sur la 1re trame d'une commande seulement, le fabricant 1 dans le 0x30, la
persistance cle/profil, l'echec de commit NVS, le refus du recul et l'epuisement du compteur.
Une radio en echec produit toujours une seule rafale.

NVS, MAC, radio et AES sont simules : ce banc ne valide ni l'AES PSA, ni la flash reelle, ni le
timing RF, ni HTTP/MQTT, ni la reception par un moteur.
