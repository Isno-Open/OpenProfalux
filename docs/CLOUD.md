# Pilotage à distance (mode cloud)

Le firmware peut, **en option**, se connecter à un service de pilotage à distance : le service
ISNO, ou votre propre instance auto-hébergée de [OpenCloudApp](https://github.com/Isno-Open/OpenCloudApp).
Le mode cloud est **additif** : le MQTT local continue de fonctionner exactement comme avant.

## Principe

- L'appareil tient **deux connexions MQTT indépendantes** : la connexion **locale** (Home Assistant,
  inchangée) et, si elle est activée, la connexion **cloud**.
- La connexion cloud est **sortante** et chiffrée (`mqtts`), avec un **certificat propre à
  l'appareil** (mTLS). Rien à ouvrir sur la box de l'utilisateur.
- Côté cloud, les topics sont préfixés par l'identifiant de l'appareil :
  `openprofalux/{device_id}/…`. Chaque appareil n'accède qu'à son propre espace.
- Les deux connexions alimentent le **même état** : une commande arrivée par l'une ou l'autre agit
  de la même façon.
- Aucun secret radio ne quitte l'appareil.

## Le local ne change pas

Le MQTT local est en production chez des utilisateurs : mêmes topics `openprofalux/…`, même
HA Discovery, même comportement. Le cloud est une connexion **séparée**, ajoutée à côté. Toute
évolution du mode cloud doit passer une non-régression du local.

## Appairage

Aucun certificat à copier à la main : l'appareil le récupère auprès du serveur choisi au moment de
l'appairage, puis le conserve. Après un reflash complet, on ré-appaire.

L'onglet **Cloud** de l'interface web de l'appareil permet de :

- choisir le **serveur** (par défaut `https://app.isno.fr`, ou l'adresse de votre instance) ;
- **appairer** l'appareil à votre compte ;
- voir le **statut** (appairé, connecté) ;
- **activer / couper** la liaison cloud sans perdre le certificat ;
- **oublier** l'appairage (efface le certificat et la configuration cloud).

Le protocole d'appairage côté serveur est décrit dans le dépôt isno-cloud.

## Ce qui se fait depuis le portail, et ce qui reste sur place

Décidé avec Olivier le 2026-11-14. Le portail montre et commande ce qui a du sens à distance ;
tout ce qui demande d'être à côté du volet ou de la télécommande reste dans l'interface locale.

**Reste dans l'interface locale :** l'appairage et l'enrôlement (apprentissage et clonage des
télécommandes, appairage virtuel Profalux, appareils Somfy virtuels), la capture et le rejeu de
trames, le réglage de la réception radio, l'appairage cloud du boîtier, la découverte du broker HA.

**Depuis le portail, par ordre d'intérêt :**

1. **Étalonnage** : temps de montée et de descente. Un volet n'est déclaré positionnel que s'il est
   étalonné (sinon sa position ne veut rien dire).
2. **Renommer** un volet ou une télécommande.
3. **Santé du boîtier**, en lecture : signal Wi-Fi, mémoire, dernier redémarrage, journal ; alerte
   pile faible des télécommandes.
4. **Mises à jour** : installer (fait), revenir à la version précédente (avec confirmation), version
   en essai.
5. **Ordre d'affichage** et **orientation** de la façade.
6. **Centrales et zones** : le portail les montre comme telles (carte « Centrale » ou « Zone », sans
   position) et en change les membres (une case par appareil du boîtier). Une centrale contient des
   volets, des portails ou des zones, jamais une autre centrale ; une zone est suivie par des volets
   simples. Créer ou supprimer une centrale ou une zone reste sur le boîtier ; une zone vidée de ses
   suiveurs redevient un volet simple, comme en local.
7. **Télécommandes connues** : sur la page du boîtier, onglet « Télécommandes » : renommer,
   supprimer ou ignorer (avec confirmation ; pas une télécommande qu'un volet utilise, le boîtier
   refuserait), réaccepter une ignorée. Le boîtier publie `remote/<serial>/info` et `ignored/<serial>`
   (retenus, vides quand elles sont retirées) et ne republie que ce qui a changé (toutes les 5 s).
8. **Sauvegarde** : télécharger, depuis le panneau « Sauvegarde » de la page du boîtier. Le portail
   publie `backup/get` ; le boîtier répond par `backup/data` (la même sauvegarde que `/api/backup`),
   sur le cloud seulement et jamais retenue, et le portail la remet au navigateur sans en garder de
   copie : elle contient les trames de référence des télécommandes. La restauration reste dans
   l'interface locale ; à distance, plus tard, avec double confirmation.
9. **Écoute permanente** : activer ou couper, depuis la page du boîtier (`listen/set`, état publié sur
   `listen/state`, republié à chaque connexion du cloud). Couper demande confirmation : sans écoute,
   le boîtier ne suit plus les volets commandés par leur télécommande.

**En lecture seule dans le portail :** le profil radio d'un volet (personnalisé ou non, fréquence),
le Wi-Fi et la connexion MQTT locale du boîtier (on ne coupe pas à distance le lien qui le relie),
l'état du mode cloud.

**Couper le cloud** ne se fait que sur le boîtier. Avant de couper, il prévient le portail qu'il ne
communiquera plus : le portail affiche « cloud désactivé sur le boîtier », et non une simple panne.

## API locale de l'appareil

| Méthode | Route | Rôle |
|---|---|---|
| `GET` | `/api/cloud/status` | `{paired, enabled, connected, server, device_id}` |
| `POST` | `/api/cloud/pair` | appaire l'appareil et démarre la liaison cloud |
| `POST` | `/api/cloud/toggle` | `{on}` : active ou coupe la liaison, certificat conservé |
| `POST` | `/api/cloud/forget` | coupe la liaison, efface certificat et configuration cloud |

## Auto-hébergement

Le serveur est configurable : il suffit de pointer l'appareil vers votre propre instance
isno-cloud. Le firmware ne dépend d'aucun service particulier.
