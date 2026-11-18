<script setup lang="ts">
// « Mes appareils » : la liste, filtree par type (les types de profiles.json), et l'edition sous la ligne. Le
// pilotage reste dans l'onglet Volets.
import { computed, ref } from 'vue';
import { state } from '../../store';
import { types, typeKey, typeIcon } from '../../profiles';
import { navigate } from '../../router';
import { SECTION, TAB, TONE } from '../../domain';
import OpCard from '../../components/base/OpCard.vue';
import ChipPicker, { type ChipOption } from '../../components/domain/ChipPicker.vue';
import OpBadge from '../../components/base/OpBadge.vue';
import DeviceRow from '../../components/domain/DeviceRow.vue';
import DeviceEdit from '../../components/domain/DeviceEdit.vue';

const ALL = '';
const filter = ref<string | string[]>(ALL);
const open = ref('');
const filters = computed<ChipOption[]>(() => [{ value: ALL, label: 'Tous' }, ...Object.entries(types()).map(([key, t]) => ({ value: key, label: t.label, icon: typeIcon(key) }))]);
/* Nombre d'appareils et plafond du firmware (profiles.json, devices_max). */
const total = computed(() => state.status?.volets.length ?? 0);
const max = computed(() => state.profiles?.devices_max);
const list = computed(() => (state.status?.volets ?? []).filter(d => !filter.value || (!d.central && !d.zone && typeKey(d) === filter.value)));
const toggle = (id: string): void => { open.value = open.value === id ? '' : id; };
</script>

<template>
  <OpCard title="Mes appareils" sub="Tes volets et télécommandes. Modifier règle l'émission (fréquence, TE, bit de répétition, RMT) propre à un appareil, ou le supprime.">
    <template #actions><OpBadge v-if="max" :tone="total >= max ? TONE.danger : TONE.neutral">{{ total }} / {{ max }}</OpBadge></template>
    <div class="devices">
      <ChipPicker v-model="filter" :options="filters" label="Filtrer par type" @update:model-value="open = ''" />
      <div v-for="d in list" :key="d.id" class="block">
        <DeviceRow :device="d" :open="open === d.id" @edit="toggle(d.id)" />
        <DeviceEdit v-if="open === d.id" :device="d" @renamed="n => (open = n)" @deleted="open = ''" />
      </div>
      <p v-if="!list.length" class="hint">Aucun appareil de ce type. Va dans
        <button type="button" class="back-link" @click="navigate(`${TAB.remotes}/${SECTION.add}`)">Ajouter un appareil</button></p>
    </div>
  </OpCard>
</template>

<style scoped>
.devices{display:grid;gap:9px}
.devices > :first-child{margin-bottom:6px}
</style>
