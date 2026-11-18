// La couche API : les ecrans ne parlent qu'a elle. Le mode mock repond avec de fausses donnees (mock*.ts), qui
// ont les memes champs que le firmware et sont verifiees contre les memes types ; la construction du firmware
// branche real*.ts, qui appelle les memes routes que l'interface d'avant.
// Volets et Appareils d'un cote, Reseau / OTA / Systeme / Support de l'autre.
import mock from './mock';
import mockSystem from './mock-system';
import real from './real';
import realSystem from './real-system';

export type Api = typeof real & typeof realSystem;

/* Fausses donnees seulement en mode mock (developpement sans carte) : __MOCK_DATA__ est pose par vite.config.ts
   a la construction, et celle du firmware ne garde que les vraies routes. */
export const usesMockData: boolean = __MOCK_DATA__;
const api: Api = __MOCK_DATA__ ? { ...mock, ...mockSystem } : { ...real, ...realSystem };
export default api;
