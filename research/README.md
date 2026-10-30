# research/ - travaux de sécurité KeeLoq (académique, reproductible)

Ce dossier rassemble la partie **recherche** du projet OpenProfalux : l'étude de
la sécurité des télécommandes Profalux 868 MHz (chiffrement KeeLoq HCS).

## À lire avant tout : ce n'est PAS la méthode du firmware

Le firmware OpenProfalux **n'utilise aucune attaque cryptographique** et **ne
récupère aucune clé**. Il fonctionne par **capture + rejeu** d'une trame
authentique par commande : le moteur Profalux n'a pas d'anti-rejeu effectif, donc
une seule trame réelle par bouton suffit, rejouable à vie. Aucune clé constructeur
n'est jamais nécessaire.

Les travaux ci-dessous répondent à une autre question, purement théorique :
**« peut-on retrouver la clé par cryptanalyse ? »**. La réponse est **non en
pratique** pour Profalux.

## Contenu

- `gpu-slide-attack/` - portage GPU (CUDA/cupy) de l'attaque **slide-meet-in-the-middle**
  de Indesteege, Preneel et al. (CRYPTO 2008), à partir du code C d'Enderlein
  (projet de semestre EPFL, 2010). Reproduction d'un résultat académique publié.

## Pourquoi c'est impraticable contre Profalux

- L'attaque slide-MITM exige **~65 536 trames** de la MÊME télécommande (paradoxe
  des anniversaires sur le compteur 16 bits). En usage réel on en capture une
  poignée : **la donnée n'est pas collectable**.
- Même avec les 65 536 trames, la récupération de clé prend **~4 jours** sur un GPU
  grand public. Sans les trames, le point de départ n'existe pas.

Autrement dit : le chemin crypto est **fermé**, et c'est justement pour ça que le
firmware passe par le rejeu. Ce dossier documente pourquoi, avec du code qui
tourne et qui a été validé bit-à-bit contre la référence.

## Cadre

Recherche menée par le propriétaire sur son propre matériel, à des fins
éducatives et défensives (comprendre ce qui protège, ou non, une installation
Profalux). Le code d'attaque de référence d'Enderlein, transmis en privé, n'est
**pas** redistribué ici.
