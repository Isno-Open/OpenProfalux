// La detection de profil de la pyramide « Ajouter un appareil » : firmware/main/web/pyramid-detect.js,
// partage avec son banc node (firmware/test/pyramid-detect). Le fichier se declare a node (module.exports)
// ou au navigateur (self.PyramidDetect). La construction le lit comme un module CommonJS (son export) ; le
// serveur de developpement le sert tel quel (il se pose alors sur self) : on accepte les deux.
import * as module from '../../../firmware/main/web/pyramid-detect.js';

const detection = (module.default && module.default.rankProfiles) ? module.default : globalThis.PyramidDetect;
export const { rankProfiles, traits } = detection;
