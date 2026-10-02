# Matériel compatible

Liste du matériel testé avec OpenProfalux, tenue à partir des retours (forum, issues).
Une ligne = un retour vérifié.

- **rejeu** : capturer une commande de la télécommande d'origine puis la réémettre.
- **enrôlement** : créer une identité que le moteur apprend, pour piloter sans capturer.

| Marque | Modèle | Fréquence | Méthode confirmée | Compteur roulant | Source |
|---|---|---|---|---|---|
| Profalux | gamme PFX (référence du projet) | 868 MHz | rejeu, enrôlement | variable selon la génération du moteur | mainteneur |
| Eveno | Zuni-R (installation 2015) | 868 MHz | rejeu | non observé (une vingtaine de rejeux successifs acceptés) | issue #3, azriek |
| Profalux | NeoSol S300 Stella AT | 868 MHz | enrôlement : essais en cours, non confirmé | à déterminer | issue #1, eleroy |
| FranciaFlex | M4G | 868 MHz | enrôlement | variable | forum HACF, Lecanard38 |

Un retour à ajouter ? Ouvrez une issue avec la marque, le modèle, l'année, la fréquence,
et ce qui a fonctionné (rejeu, enrôlement, ou les deux).

## Particularités connues

**FranciaFlex M4G** : le moteur n'accepte qu'un **nombre limité de télécommandes**. Une
fois ce nombre atteint, l'enrôlement d'un nouvel émetteur échoue sans message, et il faut
réinitialiser le moteur pour repartir.

Un signe utile pour s'en assurer : la télécommande d'origine d'un volet saturé continue
d'**apparaître dans le journal radio** du boîtier, sous le nom qu'on lui avait donné. La
réception fonctionne donc, c'est bien le moteur qui refuse. Si l'enrôlement échoue alors
que les trames arrivent, chercher du côté du moteur et non du boîtier.

Même moteur, deux points où la procédure d'enrôlement diffère de celle décrite au départ :
le volet doit être **descendu de quelques lames** et non en butée haute, et le moteur ne
fait **pas d'aller-retour de confirmation** à la fin.

## À compléter

- Modèles **Profalux** précis validés par le mainteneur, à détailler.
