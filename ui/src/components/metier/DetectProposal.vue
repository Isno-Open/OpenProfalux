<script setup>
// « Tu ne connais pas la marque ? » : on ecoute chaque radio de la carte pendant que l'utilisateur presse
// sa telecommande, on retient la bande qui a entendu (le meilleur signal), puis on PROPOSE les methodes que
// classe pyramid-detect.js. Seule la bande est observable : le reste reste un choix de l'utilisateur.
import { onMounted, ref } from 'vue';
import { etat, avecRadio, radios } from '../../store.js';
import { rankProfiles } from '../../lib/detection.js';
import { BANDE } from '../../profils.js';
import api from '../../api/index.js';
import OpProgress from '../base/OpProgress.vue';
import OpStatline from '../base/OpStatline.vue';
import ProfileChoice from './ProfileChoice.vue';

const props = defineProps({ disponibles: { type: Array, required: true } });
const emit = defineEmits(['choisir', 'annuler']);
const bande = ref(null);       // bande en cours d'ecoute
const entendu = ref(null);     // { band, rssi }
const fini = ref(false);

onMounted(async () => {
  try {
    await avecRadio(async () => {
      for (const r of (await radios()) || []) {
        bande.value = r.band;
        const d = await api.ecouter(r.idx).catch(() => null);
        if (d && d.n > 0 && (!entendu.value || d.rssi > entendu.value.rssi)) entendu.value = { band: String(d.band), rssi: d.rssi };
        if (entendu.value) break;   // une bande a entendu : inutile d'ecouter les autres
      }
    });
  } catch { /* radio occupee : le message est deja affiche */ }
  fini.value = true;
});
/* Proposition de pyramid-detect.js, limitee aux methodes que la carte sait faire. Confiant = un seul profil. */
const resultat = () => (entendu.value && etat.profils ? rankProfiles({ band: entendu.value.band }, etat.profils) : null);
const candidats = () => ((resultat() && resultat().proposed) || []).map(c => c.profil).filter(p => props.disponibles.includes(p) || props.disponibles.some(d => d.nom === p.nom));
</script>

<template>
  <div class="detection">
    <button type="button" class="lien-retour" @click="emit('annuler')">‹ Choisir moi-même</button>
    <template v-if="!fini">
      <OpStatline tone="neutre"><b>Presse et MAINTIENS ta télécommande.</b> J'écoute la bande {{ BANDE(bande || '868') }} (~15 s)…</OpStatline>
      <OpProgress label="Écoute en cours" />
    </template>
    <template v-else-if="entendu && candidats().length">
      <p class="sub">Entendu sur la bande <b>{{ BANDE(entendu.band) }}</b> ({{ entendu.rssi }} dBm).
        {{ candidats().length > 1 ? 'Plusieurs profils possibles, choisis :' : 'Profil proposé, confirme ou choisis autrement :' }}</p>
      <div class="liste">
        <ProfileChoice v-for="p in candidats()" :key="p.nom" :profil="p" :propose="candidats().length === 1" @choisir="emit('choisir', p)" />
      </div>
    </template>
    <OpStatline v-else tone="alerte">Rien entendu. Rapproche la télécommande du boîtier et réessaie, ou choisis toi-même le type et la marque.</OpStatline>
  </div>
</template>

<style scoped>
.detection{display:grid;gap:12px}
.detection > .lien-retour{justify-self:start}
.sub{margin:0;color:var(--muted);font-size:.9rem}
.liste{display:grid;gap:9px}
</style>
