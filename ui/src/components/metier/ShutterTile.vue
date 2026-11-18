<script setup>
// La tuile d'un appareil dans l'onglet Volets. Le controle est choisi d'apres le TYPE declare
// (profiles.json) : pave a trois commandes pour un volet, bouton unique pour un portail. Une centrale et
// une zone pilotent leurs volets, sans position propre.
import { computed } from 'vue';
import { etat, envoyer, typeDe, nomTelec } from '../../store.js';
import OpIcon from '../base/OpIcon.vue';
import OpBadge from '../base/OpBadge.vue';
import ShutterControl from './ShutterControl.vue';
import GateControl from './GateControl.vue';
import PositionBar from './PositionBar.vue';
import BatteryBadge from './BatteryBadge.vue';
import SignalBars from './SignalBars.vue';

const props = defineProps({
  v: { type: Object, required: true },
  rangement: Boolean,     // mode reorganisation : pilotage neutralise
  premier: Boolean,
  dernier: Boolean,
  survol: Boolean,        // une autre tuile glisse au-dessus (cible du depot)
});
defineEmits(['deplacer']);

const st = computed(() => etat.statut || {});
const type = computed(() => typeDe(props.v));
const centrale = computed(() => !!props.v.central);
const zone = computed(() => !centrale.value && !!props.v.zone);
const portail = computed(() => type.value.cle === 'portail');
const icone = computed(() => (centrale.value ? 'centrale' : zone.value ? 'zone' : portail.value ? 'portail' : 'volet'));
/* Position affichee seulement avec l'ecoute permanente : sans elle, un appui sur la vraie telecommande
   desynchronise l'estimation, et un pourcentage faux est pire qu'aucun. */
const positionnel = computed(() => !!st.value.listening && type.value.positional && !centrale.value && !zone.value);
const envoi = computed(() => (etat.envoi && etat.envoi.id === props.v.id ? etat.envoi.cmd : ''));
const occupe = computed(() => etat.radioOccupee || props.rangement);
const liste = m => (m ? m.split(',').join(', ') : 'aucun');
const captees = computed(() => Object.keys(props.v.rejeu_cmds || {}).length);
const signal = computed(() => {
  for (const f of st.value.rf || []) if ((props.v.serials || []).includes(f.serial)) return f.rssi;
  return null;
});
const ordre = (cmd, value) => envoyer(value == null ? { id: props.v.id, cmd } : { id: props.v.id, cmd, value });
</script>

<template>
  <article class="tuile" :class="{ rangement, survol }">
    <header class="haut">
      <span class="nom">
        <span v-if="rangement" class="poignee" title="Glisser pour déplacer" aria-hidden="true">⠿</span>
        <OpIcon :name="icone" size="1.35em" />
        <span class="texte">{{ v.id }}</span>
        <OpBadge v-if="centrale" tone="accent">centrale</OpBadge>
        <OpBadge v-else-if="zone" tone="accent">zone</OpBadge>
        <OpBadge v-else-if="portail">{{ v.marque || 'portail' }}</OpBadge>
      </span>
      <span v-if="rangement" class="fleches">
        <button type="button" :disabled="premier" aria-label="Monter" title="Monter" @click="$emit('deplacer', -1)">▲</button>
        <button type="button" :disabled="dernier" aria-label="Descendre" title="Descendre" @click="$emit('deplacer', 1)">▼</button>
      </span>
    </header>

    <GateControl v-if="portail" :libelle="(type.capture && type.capture[0] && type.capture[0].label) || undefined"
                 :envoi="envoi === 'up'" :disabled="occupe" @ordre="ordre" />
    <ShutterControl v-else :libelles="type.dpad" :envoi="envoi" :disabled="occupe" @ordre="ordre" />

    <PositionBar v-if="positionnel" :position="v.position" :disabled="occupe" @choisir="p => ordre('pos', p)" />

    <footer class="details">
      <template v-if="centrale">volets : {{ liste(v.members) }}</template>
      <template v-else-if="portail">rejeu de la télécommande d'origine · {{ captees }} commande{{ captees > 1 ? 's' : '' }} captée{{ captees > 1 ? 's' : '' }}</template>
      <template v-else>
        <span v-if="v.virt">identité <code>{{ nomTelec(v.virt_serial) }}</code><template v-if="(v.serials || []).length"> · télécommandes :</template></span>
        <span v-for="s in v.serials || []" :key="s" class="telec"><code>{{ nomTelec(s) }}</code><BatteryBadge :etat="(st.remote_battery || {})[s] || null" /></span>
        <span v-if="!(v.serials || []).length && !v.virt">aucune télécommande</span>
      </template>
      <span v-if="signal != null" class="recu">reçu <SignalBars :rssi="signal" /></span>
      <span v-if="zone" class="ligne">suiveurs : {{ liste(v.members) }}</span>
      <span v-else-if="(v.follows || []).length" class="ligne">suit : {{ v.follows.join(', ') }}</span>
    </footer>
  </article>
</template>

<style scoped>
.tuile{display:grid;gap:14px;background:var(--surface);background-image:var(--surface-grad);border:1px solid var(--border);border-radius:var(--radius);padding:20px;box-shadow:var(--shadow);min-width:0}
.haut{display:flex;align-items:center;justify-content:space-between;gap:10px;min-height:28px}
.nom{display:flex;align-items:center;gap:9px;font-weight:650;font-size:1.08rem;min-width:0}
.texte{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.fleches{display:inline-flex;gap:6px}
.fleches button{width:34px;height:30px;border:1px solid var(--border);background:var(--surface-2);color:var(--text);border-radius:8px;cursor:pointer}
.fleches button:disabled{opacity:.35;cursor:default}
.details{display:flex;flex-wrap:wrap;align-items:center;gap:6px 10px;font-size:.76rem;color:var(--muted)}
.details code{background:var(--surface-2);padding:1px 6px;border-radius:5px;border:1px solid var(--border)}
.telec{display:inline-flex;align-items:center;gap:5px}
.rangement{outline:1px dashed var(--accent-line);outline-offset:3px;cursor:grab}
.survol{outline:2px solid var(--accent)}
.poignee{color:var(--muted);cursor:grab;font-size:1.1rem;line-height:1}
.recu{display:inline-flex;align-items:center;gap:5px}
.ligne{flex-basis:100%}
</style>
