# Banc de test cloud_enroll

Décisions du cloud sorties des handlers HTTP pour être testables (`firmware/main/cloud_enroll.c`) :

- `cloud_bundle_ok` : le bundle reçu à l'appairage (`/api/cloud/pair`) est-il exploitable (device_id,
  broker_url, CA, cert, clé tous présents et non vides) ? Refuser évite un provisioning à moitié fait.
- `cloud_want` : le cloud doit-il tourner (Wi-Fi up + URL broker + interrupteur `cloud_on`) ? Utilisé
  au boot ET par `/api/cloud/toggle`.

Unité PURE, testée sur PC :

    cc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address -g \
       -I main -I ../../main main/test_main.c ../../main/cloud_enroll.c -o cloud_enroll_host_test
    ./cloud_enroll_host_test
