<script setup>
// « Ajouter un appareil » : la pyramide TYPE -> MARQUE -> METHODE, rendue depuis profiles.json (source
// unique). Chaque choix est dans l'adresse (#remotes/add/volet/Profalux) : un F5 ou un lien garde l'etape.
// Une marque qui n'a qu'une methode mene droit a son parcours. On ne montre que les methodes dont la carte
// a la radio (une carte 868 seule ne propose pas le portail 433).
import { computed, onMounted, ref } from 'vue';
import { route, aller } from '../../router.js';
import { radios } from '../../store.js';
import { profils, types, iconeType, adresseMethode, BANDE } from '../../profils.js';
import OpCard from '../base/OpCard.vue';
import OpIcon from '../base/OpIcon.vue';
import OpSteps from '../base/OpSteps.vue';
import OpBadge from '../base/OpBadge.vue';
import ProfileChoice from './ProfileChoice.vue';
import DetectProposal from './DetectProposal.vue';

const bandes = ref(null);
onMounted(async () => { const r = await radios(); bandes.value = r ? new Set(r.map(x => String(x.band))) : null; });

const dispo = computed(() => profils().filter(p => !p.band || !bandes.value || bandes.value.has(String(p.band))));
const lire = i => (route.suite[i] ? decodeURIComponent(route.suite[i]) : null);
const type = computed(() => lire(0));
const marque = computed(() => lire(1));
const etape = computed(() => (!type.value ? 0 : !marque.value ? 1 : 2));
const detection = ref(false);

const listeTypes = computed(() => Object.entries(types()).map(([cle, t]) => ({ cle, label: t.label, n: dispo.value.filter(p => p.type === cle).length })));
const marques = computed(() => {
  const l = dispo.value.filter(p => p.type === type.value);
  return [...new Set(l.map(p => p.marque))].map(m => ({ m, bande: l.find(p => p.marque === m).band, n: l.filter(p => p.marque === m).length }));
});
const methodes = computed(() => dispo.value.filter(p => p.type === type.value && p.marque === marque.value));
const labelType = k => (types()[k] && types()[k].label) || k;

const enc = encodeURIComponent;
function choisirMarque(m) {
  const f = dispo.value.filter(p => p.type === type.value && p.marque === m);
  aller(f.length === 1 ? adresseMethode(f[0]) : `remotes/add/${enc(type.value)}/${enc(m)}`);
}
const choisirMethode = p => aller(adresseMethode(p));
</script>

<template>
  <OpCard title="Ajouter un appareil" sub="Choisis le type d'appareil, puis la marque, puis la méthode. On te guide étape par étape.">
    <div class="pyramide">
      <DetectProposal v-if="detection" :disponibles="dispo" @annuler="detection = false" @choisir="choisirMethode" />
      <template v-else>
        <OpSteps :etapes="['Type', 'Marque', 'Méthode']" :actuelle="etape" />
        <nav v-if="etape > 0" class="fil" aria-label="Où tu en es">
          <button type="button" class="lien-retour" @click="aller('remotes/add')">‹ Recommencer</button>
          <span>· {{ labelType(type) }}</span><span v-if="marque">· {{ marque }}</span>
        </nav>

        <template v-if="etape === 0">
          <p class="question">Quel type d'appareil ?</p>
          <div class="tuiles">
            <button v-for="t in listeTypes" :key="t.cle" type="button" class="tuile" :disabled="!t.n" @click="aller(`remotes/add/${enc(t.cle)}`)">
              <OpIcon :name="iconeType(t.cle)" size="2.6em" />
              <b>{{ t.label }}</b>
              <span class="hint">{{ t.n ? `${t.n} méthode${t.n > 1 ? 's' : ''}` : 'bientôt' }}</span>
            </button>
          </div>
          <p class="hint">Tu ne connais pas la marque ?
            <button type="button" class="lien" @click="detection = true">Détecter en pressant la télécommande</button></p>
        </template>

        <template v-else-if="etape === 1">
          <p class="question">Quelle marque ?</p>
          <div class="tuiles marques">
            <button v-for="m in marques" :key="m.m" type="button" class="tuile marque" @click="choisirMarque(m.m)">
              <b>{{ m.m }}</b>
              <span class="meta"><OpBadge v-if="m.bande">{{ BANDE(m.bande) }}</OpBadge><span class="hint">{{ m.n > 1 ? `${m.n} méthodes` : '1 méthode' }}</span></span>
            </button>
          </div>
        </template>

        <template v-else>
          <p class="question">Comment l'ajouter ?</p>
          <div class="liste">
            <ProfileChoice v-for="p in methodes" :key="p.nom" :profil="p" @choisir="choisirMethode" />
          </div>
        </template>
      </template>
    </div>
  </OpCard>
</template>

<style scoped>
.pyramide{display:grid;gap:14px}
.fil{display:flex;flex-wrap:wrap;align-items:center;gap:6px;font-size:.85rem;color:var(--muted)}
.question{margin:4px 0 0;font-weight:600}
.tuiles{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}
.tuile{display:grid;justify-items:center;gap:6px;padding:22px 14px;border:1px solid var(--border);border-radius:16px;background:var(--surface-2);color:var(--text);font:inherit;cursor:pointer;transition:.12s}
.tuile:hover:not(:disabled){border-color:var(--accent);transform:translateY(-1px)}
.tuile:disabled{opacity:.45;cursor:default}
.tuile.marque{justify-items:start;padding:16px 18px}
.meta{display:flex;align-items:center;gap:8px}
.liste{display:grid;gap:9px}
.lien{background:none;border:0;padding:0;color:var(--accent);font:inherit;font-weight:600;cursor:pointer;text-decoration:underline}
</style>
