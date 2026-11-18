// La couche API : les ecrans ne parlent qu'a elle. La maquette repond avec de fausses donnees (mock*.js),
// qui ont les memes champs que le firmware ; le vrai port n'aura qu'a brancher reelle*.js
// (VITE_API=reelle), qui appelle les memes routes que l'interface d'aujourd'hui.
// Volets et Appareils d'un cote, Reseau / OTA / Systeme / Support de l'autre.
import mock from './mock.js';
import mockSysteme from './mock-systeme.js';
import reelle from './reelle.js';
import reelleSysteme from './reelle-systeme.js';

/* Fausses donnees seulement si on les demande (developpement sans carte) : la construction du firmware prend
   toujours les vraies routes. */
export const fausseDonnees = import.meta.env.VITE_API === 'mock';
export default fausseDonnees ? { ...mock, ...mockSysteme } : { ...reelle, ...reelleSysteme };
