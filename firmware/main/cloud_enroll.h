#pragma once
/* Décisions du cloud sorties des handlers pour être testables sur PC (unités PURES, aucune
 * dépendance ESP-IDF) : cf firmware/test/cloud-enroll.
 *  - cloud_bundle_ok : le bundle reçu à l'appairage est-il exploitable (pas de provisioning à
 *    moitié fait) ? Utilisé par le handler /api/cloud/pair.
 *  - cloud_want       : le cloud doit-il tourner ? Utilisé au boot ET par /api/cloud/toggle. */
#include <stdbool.h>

/* Vrai si les cinq champs du bundle d'enrôlement sont présents ET non vides. */
bool cloud_bundle_ok(const char *device_id, const char *broker_url,
                     const char *ca, const char *cert, const char *key);

/* Vrai si le cloud doit être actif : Wi-Fi up, URL broker posée, et interrupteur (cloud_on) actif. */
bool cloud_want(bool wifi_up, const char *cloud_uri, bool cloud_on);
