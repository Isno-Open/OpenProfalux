// Un routeur minimal par le hash de l'adresse, comme l'interface d'aujourd'hui : #onglet/section/suite.
// Les anciennes adresses #wifi, #mqtt et #cloud (dont celle du portail captif, /#wifi) menent a leur section
// de Reseau.
import { reactive } from 'vue';
import { ONGLETS } from './nav.js';

const ALIAS = { wifi: ['reseau', 'wifi'], mqtt: ['reseau', 'mqtt'], cloud: ['reseau', 'cloud'] };

export const route = reactive({ onglet: 'control', section: null, suite: [] });

function lire() {
  let [o, s, ...suite] = (location.hash || '#control').slice(1).split('/');
  if (ALIAS[o]) [o, s] = ALIAS[o];
  const onglet = ONGLETS.find(t => t.id === o) || ONGLETS[0];
  const sections = onglet.sections || [];
  route.onglet = onglet.id;
  route.section = sections.length ? (sections.some(x => x.id === s) ? s : sections[0].id) : null;
  route.suite = sections.some(x => x.id === s) ? suite : [];
}

export function aller(chemin) {
  if (location.hash.slice(1) === chemin) lire();
  else location.hash = chemin;
}

addEventListener('hashchange', lire);
lire();
