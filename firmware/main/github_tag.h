/*
 * Recherche de "tag_name" dans la reponse de l'API GitHub, lue par morceaux de taille quelconque.
 * Unite PURE (aucune dependance ESP-IDF), testee sur PC : firmware/test/github-tag.
 *
 * Pourquoi par morceaux : garder la reponse entiere coutait 6 Ko (plus 4 Ko de tampon HTTP) sur
 * l'ATOM, qui en manquait (mesure le 2026-11-13). Ici, une fenetre de quelques dizaines d'octets.
 */
#ifndef GITHUB_TAG_H
#define GITHUB_TAG_H

#include <stdbool.h>
#include <stddef.h>

#define GITHUB_TAG_MAX 24   /* "v0.2.16-cloudtest3" et le zero final tiennent */

typedef struct {
    char win[GITHUB_TAG_MAX + 32];   /* fin du morceau precedent : la cle ou la valeur a cheval */
    size_t have;
    char tag[GITHUB_TAG_MAX];        /* resultat, vide tant que rien n'est trouve */
    bool done;                       /* trouve, ou valeur trop longue : inutile de lire la suite */
} github_tag_t;

void github_tag_init(github_tag_t *st);
/* Donne le morceau suivant. Rend true quand la lecture peut s'arreter (st->tag rempli, ou valeur
 * refusee car trop longue, st->tag vide). */
bool github_tag_feed(github_tag_t *st, const char *data, size_t n);

#endif
