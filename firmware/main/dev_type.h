/* Type d'appareil (dev_type_t), dans un en-tete SANS dependance : cfg_model.h l'inclut, et les bancs
 * sur PC (test/cover-attrs) le compilent seul. */
#pragma once

/* Type d'appareil : ce qu'on PILOTE, orthogonal au protocole d'emission. Il decide le vocabulaire
 * de commande et les libelles de l'UI (un volet monte/descend, un portail ouvre/ferme), pas la
 * facon d'emettre. 0 = volet par defaut : les appareils deja persistes, sans ce champ, restent des
 * volets. On ne declare QUE les types qu'on pilote : l'eclairage s'ajoutera le jour ou on commande
 * une lumiere (ajout seul, sans casser le NVS). */
typedef enum {
    TYPE_VOLET   = 0,
    TYPE_PORTAIL = 1,
} dev_type_t;
