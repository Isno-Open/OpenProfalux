#ifndef TLS_BUDGET_H
#define TLS_BUDGET_H
/* Taille de la reserve TLS pour la configuration du boitier : HA en mqtts (avec ou sans
 * certificat client), cloud actif, verification GitHub automatique. 0 = aucun TLS attendu.
 * Unite pure (tls_budget.c), testee sur PC : firmware/test/tls-budget. */
#include <stdbool.h>
#include <stddef.h>
size_t tls_pool_need(bool local_tls, bool local_mtls, bool cloud, bool github_auto);
#endif
