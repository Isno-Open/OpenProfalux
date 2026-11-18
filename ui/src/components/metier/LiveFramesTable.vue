<script setup>
// Les trames RF captees, les plus recentes d'abord, chargees par pages quand on descend. Le nom de la
// telecommande (ou du volet) et le bouton appris sont resolus depuis le statut ; chaque trame se rejoue.
// La premiere page se relit toutes les 5 s tant qu'on n'a pas defile (sinon on garde la position).
import { onMounted, onUnmounted, ref } from 'vue';
import { etat, toast } from '../../store.js';
import { aller } from '../../router.js';
import api from '../../api/index.js';
import OpTable from '../base/OpTable.vue';
import OpBadge from '../base/OpBadge.vue';

const PAGE = 50;
const trames = ref([]);
const total = ref(0);
let charge = false;
async function suite(depart) {
  if (charge || (!depart && trames.value.length >= total.value && total.value)) return;
  charge = true;
  const d = await api.rf(depart ? 0 : trames.value.length, PAGE).catch(() => null);
  charge = false;
  if (!d) return;
  total.value = d.total;
  trames.value = depart ? d.frames : [...trames.value, ...d.frames];
}
let minuteur = null;
onMounted(() => {
  suite(true);
  minuteur = setInterval(() => { if (!document.hidden && trames.value.length <= PAGE) suite(true); }, 5000);
});
onUnmounted(() => clearInterval(minuteur));
defineExpose({ recharger: () => suite(true) });

const volets = () => (etat.statut && etat.statut.volets) || [];
const voletDe = s => (volets().find(v => (v.serials || []).includes(s)) || {}).id || '';
const nom = s => ((etat.statut && etat.statut.remotes) || {})[s] || voletDe(s);
const BOUTON = { up: 'Haut', down: 'Bas', stop: 'Stop' };
function bouton(serial, hex) {
  const b = parseInt(hex, 16);
  for (const v of volets()) {
    if (!(v.serials || []).includes(serial)) continue;
    for (const k of ['up', 'down', 'stop']) if (v.cmd && v.cmd[k] && v.cmd[k].b === b) return BOUTON[k];
  }
  return '';
}
const heure = t => new Date(t * 1000).toLocaleString('fr-FR', { day: '2-digit', month: '2-digit', hour: '2-digit', minute: '2-digit', second: '2-digit' });
async function rejouer(f) {
  const r = await api.rejouerTrame(f.serial, f.hop).catch(() => null);
  toast(r && r.ok ? 'Trame rejouée' : 'Échec du rejeu');
}
const COLONNES = [
  { cle: 't', label: 'Heure', mono: true }, { cle: 'serial', label: 'Télécommande' }, { cle: 'button', label: 'Bouton' },
  { cle: 'hop', label: 'Hopping', mono: true }, { cle: 'rssi', label: 'RSSI', mono: true, droite: true }, { cle: 'rejeu', label: '' },
];
</script>

<template>
  <OpTable :colonnes="COLONNES" :lignes="trames" cle="hop" hauteur="380px" vide="Aucune trame captée." @bas="suite(false)">
    <template #cellule-t="{ ligne }">{{ heure(ligne.t) }}</template>
    <template #cellule-serial="{ ligne }">
      <button v-if="voletDe(ligne.serial)" type="button" class="lien" :title="`${ligne.serial} : paramétrer ${voletDe(ligne.serial)}`"
              @click="aller(`remotes/learn/-/${encodeURIComponent(voletDe(ligne.serial))}`)">{{ nom(ligne.serial) }}</button>
      <span v-else-if="nom(ligne.serial)" :title="ligne.serial">{{ nom(ligne.serial) }}</span>
      <code v-else class="m" title="télécommande non nommée">{{ ligne.serial }}</code></template>
    <template #cellule-button="{ ligne }"><OpBadge :tone="bouton(ligne.serial, ligne.button) ? 'accent' : 'neutre'">{{ bouton(ligne.serial, ligne.button) || `0x${ligne.button}` }}</OpBadge></template>
    <template #cellule-hop="{ ligne }">0x{{ ligne.hop }}</template>
    <template #cellule-rssi="{ ligne }">{{ ligne.rssi }} dBm</template>
    <template #cellule-rejeu="{ ligne }"><button type="button" class="rejeu" title="Rejouer cette trame" aria-label="Rejouer cette trame" @click="rejouer(ligne)">▶</button></template>
  </OpTable>
  <p class="hint">{{ total ? `${Math.min(trames.length, total)} / ${total} trames${trames.length < total ? ' · descends pour charger la suite' : ''}` : '' }}</p>
</template>

<style scoped>
.m{font-family:var(--mono);font-size:.8rem;color:var(--muted)}
.rejeu{border:1px solid var(--border);background:var(--surface);color:var(--text);border-radius:7px;width:30px;height:26px;cursor:pointer;font-size:.7rem}
.rejeu:hover{border-color:var(--accent);color:var(--accent)}
.lien{background:none;border:0;padding:0;color:var(--accent);font:inherit;cursor:pointer;text-decoration:underline}
</style>
