<script setup>
// Les telecommandes captees par le boitier (meme celle d'un voisin), et celles qu'il ignore. Une
// telecommande supprimee revient si elle est de nouveau captee ; une telecommande ignoree ne revient plus.
import { computed, ref } from 'vue';
import { etat, rafraichir, toast } from '../../store.js';
import { aVirgule } from '../../profils.js';
import api from '../../api/index.js';
import OpCard from '../../components/base/OpCard.vue';
import OpBadge from '../../components/base/OpBadge.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import OpTagBox from '../../components/base/OpTagBox.vue';
import SignalBars from '../../components/metier/SignalBars.vue';
import BatteryBadge from '../../components/metier/BatteryBadge.vue';

const st = computed(() => etat.statut || {});
const telecs = computed(() => Object.entries(st.value.remotes || {}));
const max = computed(() => st.value.remotes_max || 16);
const pleine = computed(() => telecs.value.length >= max.value);
const ignorees = computed(() => st.value.remotes_ignored || []);
const rssi = s => { const f = (st.value.rf || []).find(x => x.serial === s); return f ? f.rssi : null; };
const utilisee = s => { const v = (st.value.volets || []).find(x => (x.serials || []).includes(s)); return v ? v.id : ''; };
const libelle = (s, n) => (n ? `« ${n} » (${s})` : s);

const creation = ref('');    // serial pour lequel on saisit le nom du volet
const nomVolet = ref('');
function ouvrirCreation(serial, nom) { creation.value = serial; nomVolet.value = nom; }
async function creerVolet(serial) {
  const n = nomVolet.value.trim();
  if (!n || aVirgule(n)) { toast('Donne un nom au volet, sans virgule'); return; }
  try {
    const d = await api.voletDepuisTelec(serial, n);
    const SYM = { up: '▲', stop: '■', down: '▼' };
    const pris = (d.learned || '').split(',').filter(Boolean);
    const manque = ['up', 'stop', 'down'].filter(a => !pris.includes(a));
    toast(`Volet « ${n} » créé : ${pris.map(a => SYM[a]).join(' ')} appris${manque.length ? ` · ${manque.map(a => SYM[a]).join(' ')} à apprendre` : ''}`);
    creation.value = '';
    rafraichir();
  } catch (e) { toast(`Refusé : ${e}`); }
}
async function nommer(serial, nom) { await api.telecNommer(serial, nom.trim()); rafraichir(); }
/* Sans confirmation : une telecommande supprimee revient d'un appui, une ignoree d'un clic sur sa croix. */
async function supprimer(serial, nom) {
  try { await api.telecSupprimer(serial); toast(`${libelle(serial, nom)} supprimée`); } catch (e) { toast(`Refusé : ${e}`); }
  rafraichir();
}
async function ignorer(serial, nom) {
  try { await api.telecIgnorer(serial); toast(`${libelle(serial, nom)} ignorée`); } catch (e) { toast(`Refusé : ${e}`); }
  rafraichir();
}
async function ajouterIgnoree(t) {
  try { await api.telecIgnorer(t.trim()); toast('Télécommande ignorée'); } catch (e) { toast(`Refusé : ${e}`); }
  rafraichir();
}
async function retablir(s) { await api.telecRetablir(s); toast(`${s} n'est plus ignorée`); rafraichir(); }
</script>

<template>
  <OpCard title="Télécommandes captées" sub="Toute télécommande captée par le boîtier, même celle d'un voisin, s'enregistre ici. Supprime celles qui ne servent pas : une télécommande supprimée revient si elle est de nouveau captée.">
    <template #actions><OpBadge :tone="pleine ? 'danger' : 'neutre'">{{ telecs.length }} / {{ max }}</OpBadge></template>
    <div class="captees">
      <OpStatline v-if="pleine" tone="danger"><b>Liste pleine :</b> une nouvelle télécommande ne pourra pas être enregistrée. Supprime une télécommande inutile.</OpStatline>
      <div v-for="[serial, nom] in telecs" :key="serial" class="telec">
        <code class="serial">{{ serial }}</code>
        <input class="op-input nom" :value="nom" placeholder="nom…" :aria-label="`Nom de la télécommande ${serial}`" @change="e => nommer(serial, e.target.value)">
        <span class="etat">
          <SignalBars v-if="rssi(serial) != null" :rssi="rssi(serial)" />
          <BatteryBadge :etat="(st.remote_battery || {})[serial] || null" />
        </span>
        <span class="usage">
          <OpBadge v-if="utilisee(serial)" tone="accent">{{ utilisee(serial) }}</OpBadge>
          <span v-else-if="creation === serial" class="rangee creation">
            <input v-model="nomVolet" class="op-input" placeholder="nom du volet" aria-label="Nom du volet à créer"
                   @keydown.enter="creerVolet(serial)" @keydown.esc="creation = ''">
            <OpButton small variant="primary" @click="creerVolet(serial)">Créer</OpButton>
          </span>
          <OpButton v-else small title="Créer un volet avec les dernières trames de cette télécommande dans le journal RF" @click="ouvrirCreation(serial, nom)">+ Créer le volet</OpButton>
        </span>
        <span class="actions">
          <OpButton small variant="ghost" title="Ne plus l'enregistrer quand elle est captée" @click="ignorer(serial, nom)">Ignorer</OpButton>
          <OpButton small variant="danger" @click="supprimer(serial, nom)">Supprimer</OpButton>
        </span>
      </div>
      <p v-if="!telecs.length" class="hint">Aucune télécommande enregistrée.</p>

      <h3 class="titre-section">Télécommandes ignorées</h3>
      <p class="hint">Le boîtier ne les enregistre plus quand il les capte (celle d'un voisin, par exemple). Retire-en une avec sa croix, ou ajoute un serial à la main.</p>
      <OpTagBox :tags="ignorees" mono placeholder="0x… puis Entrée" @ajouter="ajouterIgnoree" @retirer="retablir" />
    </div>
  </OpCard>
</template>

<style scoped>
.captees{display:grid;gap:9px}
.telec{display:grid;grid-template-columns:auto minmax(120px,1fr) auto minmax(150px,auto) auto;align-items:center;gap:10px;padding:9px 12px;border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surface-2)}
.serial{font-family:var(--mono);font-size:.82rem;padding:2px 7px;border-radius:6px;background:var(--surface);border:1px solid var(--border)}
.nom{padding-top:6px;padding-bottom:6px}
.etat,.actions{display:inline-flex;align-items:center;gap:6px}
.usage{display:flex;align-items:center;min-width:0}
.creation{flex-wrap:nowrap}
.creation .op-input{min-width:0;width:140px;padding-top:5px;padding-bottom:5px}
@media (max-width:760px){
  .telec{grid-template-columns:auto 1fr auto}
  .usage{grid-column:1 / 3}
  .actions{grid-column:3;justify-content:flex-end}
}
/* Telephone : serial et signal, puis le nom, puis l'usage et les actions sur une meme ligne. */
@media (max-width:420px){
  .telec{grid-template-columns:1fr auto;gap:8px}
  .serial{grid-area:1 / 1;justify-self:start}
  .etat{grid-area:1 / 2}
  .nom{grid-area:2 / 1 / 3 / 3}
  .usage{grid-area:3 / 1}
  .actions{grid-area:3 / 2}
  .creation .op-input{width:110px}
}
</style>
