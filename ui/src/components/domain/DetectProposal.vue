<script setup lang="ts">
// « Tu ne connais pas la marque ? » : on ecoute chaque radio de la carte pendant que l'utilisateur presse sa
// telecommande, on retient la bande qui a entendu (le meilleur signal), puis on PROPOSE les methodes que classe
// pyramid-detect.js. Seule la bande est observable : le reste reste un choix de l'utilisateur.
import { computed, onMounted, ref } from 'vue';
import { state, withRadio, radios } from '../../store';
import { rankProfiles } from '../../lib/detection';
import { bandLabel, profileById, type ProfileView } from '../../profiles';
import api from '../../api/index';
import { BAND, TONE } from '../../domain';
import OpProgress from '../base/OpProgress.vue';
import OpStatline from '../base/OpStatline.vue';
import ProfileChoice from './ProfileChoice.vue';

const props = defineProps<{ available: ProfileView[] }>();
const emit = defineEmits<{ choose: [profile: ProfileView]; cancel: [] }>();
const band = ref<string | null>(null);                             // bande en cours d'ecoute
const heard = ref<{ band: string; rssi: number } | null>(null);
const finished = ref(false);

onMounted(async () => {
  try {
    await withRadio(async () => {
      for (const r of (await radios()) || []) {
        band.value = r.band;
        const d = await api.listen(r.idx).catch(() => null);
        const rssi = d?.rssi ?? -200;
        if (d && d.n > 0 && (!heard.value || rssi > heard.value.rssi)) heard.value = { band: String(d.band), rssi };
        if (heard.value) break;   // une bande a entendu : inutile d'ecouter les autres
      }
    });
  } catch { /* radio occupee : le message est deja affiche */ }
  finished.value = true;
});
/* Le trait que la capture observe : la bande de la radio qui a entendu (catalogue, traits[].detected). */
const BAND_TRAIT = 'radio.band';
/* Proposition de pyramid-detect.js, limitee aux methodes que la carte sait faire. Confiant = un seul profil. */
const candidates = computed<ProfileView[]>(() => {
  if (!heard.value || !state.profiles) return [];
  const ranking = rankProfiles({ [BAND_TRAIT]: heard.value.band }, state.profiles);
  return ranking.proposed.flatMap(c => profileById(c.profile.id) ?? []).filter(p => props.available.some(d => d.id === p.id));
});
</script>

<template>
  <div class="detection">
    <button type="button" class="back-link" @click="emit('cancel')">‹ Choisir moi-même</button>
    <template v-if="!finished">
      <OpStatline :tone="TONE.neutral"><b>Presse et MAINTIENS ta télécommande.</b> J'écoute la bande {{ bandLabel(band || BAND.mhz868) }} (~15 s)…</OpStatline>
      <OpProgress label="Écoute en cours" />
    </template>
    <template v-else-if="heard && candidates.length">
      <p class="sub">Entendu sur la bande <b>{{ bandLabel(heard.band) }}</b> ({{ heard.rssi }} dBm).
        {{ candidates.length > 1 ? 'Plusieurs profils possibles, choisis :' : 'Profil proposé, confirme ou choisis autrement :' }}</p>
      <div class="list">
        <ProfileChoice v-for="p in candidates" :key="p.id" :profile="p" :proposed="candidates.length === 1" @choose="emit('choose', p)" />
      </div>
    </template>
    <OpStatline v-else :tone="TONE.warning">Rien entendu. Rapproche la télécommande du boîtier et réessaie, ou choisis toi-même le type et la marque.</OpStatline>
  </div>
</template>

<style scoped>
.detection{display:grid;gap:12px}
.detection > .back-link{justify-self:start}
.sub{margin:0;color:var(--muted);font-size:.9rem}
.list{display:grid;gap:9px}
</style>
