# Banc de test cloud_topic

Mapping des topics pour la connexion **cloud** (`firmware/main/cloud_topic.c`) : le prefixe par
`device_id` qui confine chaque boitier a `openprofalux/{device_id}/#` (isolation ACL du broker), et
le retour en forme locale des commandes recues. Unite PURE, testee sur PC.

    cc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address -g \
       -I main -I ../../main main/test_main.c ../../main/cloud_topic.c -o cloud_topic_host_test
    ./cloud_topic_host_test

Couvre : prefixe des topics sortants, rejet des topics hors `openprofalux/` (Discovery HA local-only),
non-double-prefixe, re-prefixe d'un prefixe etranger (anti-usurpation), rejet des commandes d'un
autre device, bornes de tampon.
