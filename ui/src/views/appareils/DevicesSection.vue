<script setup>
// « Mes appareils » : la liste, filtree par type (les types de profiles.json), et l'edition sous la ligne.
// Le pilotage reste dans l'onglet Volets.
import { computed, ref } from 'vue';
import { etat } from '../../store.js';
import { types, cleType, iconeType } from '../../profils.js';
import { aller } from '../../router.js';
import OpCard from '../../components/base/OpCard.vue';
import ChipPicker from '../../components/metier/ChipPicker.vue';
import OpBadge from '../../components/base/OpBadge.vue';
import DeviceRow from '../../components/metier/DeviceRow.vue';
import DeviceEdit from '../../components/metier/DeviceEdit.vue';

const filtre = ref('');
const ouvert = ref('');
const filtres = computed(() => [{ value: '', label: 'Tous' }, ...Object.entries(types()).map(([k, t]) => ({ value: k, label: t.label, icon: iconeType(k) }))]);
/* Nombre d'appareils et plafond du firmware (profiles.json, devices_max). */
const total = computed(() => ((etat.statut && etat.statut.volets) || []).length);
const max = computed(() => etat.profils && etat.profils.devices_max);
const liste = computed(() => ((etat.statut && etat.statut.volets) || []).filter(v => !filtre.value || (!v.central && !v.zone && cleType(v) === filtre.value)));
</script>

<template>
  <OpCard title="Mes appareils" sub="Tes volets et télécommandes. Modifier règle l'émission (fréquence, TE, bit de répétition, RMT) propre à un appareil, ou le supprime.">
    <template #actions><OpBadge v-if="max" :tone="total >= max ? 'danger' : 'neutre'">{{ total }} / {{ max }}</OpBadge></template>
    <div class="appareils">
      <ChipPicker v-model="filtre" :options="filtres" label="Filtrer par type" @update:model-value="ouvert = ''" />
      <div v-for="v in liste" :key="v.id" class="bloc">
        <DeviceRow :v="v" :ouvert="ouvert === v.id" @modifier="ouvert = ouvert === v.id ? '' : v.id" />
        <DeviceEdit v-if="ouvert === v.id" :v="v" @renomme="n => (ouvert = n)" @supprime="ouvert = ''" />
      </div>
      <p v-if="!liste.length" class="hint">Aucun appareil de ce type. Va dans
        <button type="button" class="lien-retour" @click="aller('remotes/add')">Ajouter un appareil</button></p>
    </div>
  </OpCard>
</template>

<style scoped>
.appareils{display:grid;gap:9px}
.appareils > :first-child{margin-bottom:6px}
</style>
