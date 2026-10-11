# Matériel compatible

Liste du matériel testé avec OpenProfalux, tenue à partir des retours (forum, issues).
Une ligne = un retour vérifié.

- **rejeu** : capturer une commande de la télécommande d'origine puis la réémettre.
- **enrôlement** : créer une identité que le moteur apprend, pour piloter sans capturer.

| Marque | Modèle | Fréquence | Méthode confirmée | Compteur roulant | Source |
|---|---|---|---|---|---|
| Profalux | gamme PFX (référence du projet) | 868 MHz (868,350 à 868,425 selon la génération) | rejeu, enrôlement | variable selon la génération du moteur | mainteneur, issue #34 |
| Eveno | Zuni-R (installation 2015) | 868,33 MHz (profil radio `eveno`) | rejeu | non observé (une vingtaine de rejeux successifs acceptés) | issue #3, azriek |
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

**Eveno Zuni-R** : les télécommandes émettent à **868,33 MHz**, environ 100 kHz sous
Profalux (868,425 MHz), au bord de la bande passante de réception. Avec le profil Profalux,
elles ne sont pas captées, même collées à l'antenne. Choisir le profil radio **Eveno** dans
Système > Appareil (ou `"radio_profile":"eveno"` dans `POST /api/config`). Pour une autre
marque, le profil **personnalisé** prend une fréquence en kHz, dans la plage autorisée de la
carte.

**Profalux, fréquence selon la génération** : la porteuse Profalux n'est pas unique, elle a
baissé au fil des générations. Récente ≈ **868,425 MHz** (profil `profalux` par défaut), vers
2011-2012 ≈ **868,400 MHz**, plus anciennes ≈ **868,350 MHz**. Symptôme typique : le boîtier
capte quelque chose (RSSI fort, log « entête HCS OK mais N bits »), mais aucune trame
complète, et seules les télécommandes récentes s'apprennent. Dans ce cas, choisir
**Fréquence personnalisée** dans Système > Appareil et essayer **868350** puis **868400**, en
relançant l'apprentissage à chaque valeur, télécommande près de l'antenne. Une fois
l'identité captée, le pilotage fonctionne même revenu sur le profil Profalux. (issue #34 :
valentinni192 capté à 868,400 ; marcusboludo, 9 anciens volets, à 868,350.)

## À compléter

- Modèles **Profalux** précis validés par le mainteneur, à détailler.
