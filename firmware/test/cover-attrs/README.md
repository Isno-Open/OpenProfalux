# Banc de test cover_attrs

Description d'un appareil pour le portail cloud (`firmware/main/cover_attrs.c`) : le message retenu
`openprofalux/cover/<slug>/attrs`, JSON `{"type":"volet"|"portail","positional":bool,"uid":"..."}`, que le
portail lit pour choisir la carte. Unite PURE, testee sur PC.

`uid` est l'identifiant STABLE de l'appareil : le slug change quand on le renomme, pas lui. Le banc
eprouve aussi le slug d'un nom (`cover_slug`) et le choix de cet identifiant (`cover_uid_pick`).

    cc -std=gnu11 -Wall -Wextra -fsanitize=address -g \
       -I ../../main main/test_main.c ../../main/cover_attrs.c -o cover_attrs_host_test
    ./cover_attrs_host_test ../../main/web/profiles.json

Couvre : JSON d'un volet et d'un portail, refus d'un type inconnu et d'un tampon trop petit, et la
COHERENCE des noms et codes de types avec `profiles.json` (source du vocabulaire) : renommer un type
d'un cote sans l'autre fait echouer le banc.
