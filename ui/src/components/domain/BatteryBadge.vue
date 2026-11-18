<script setup lang="ts">
// Pile d'une telecommande (bit VLOW des trames) : rien tant qu'aucune trame n'a ete vue.
import OpIcon from '../base/OpIcon.vue';
import OpBadge from '../base/OpBadge.vue';
import type { BatteryState } from '../../api/types';
import { TONE } from '../../domain';
import { ICON } from '../../icons';
withDefaults(defineProps<{ battery?: BatteryState | null }>(), { battery: null });
</script>

<template>
  <OpBadge v-if="battery?.low" :tone="TONE.danger" title="Bit « pile faible » vu sur plusieurs appuis d'affilée">
    <OpIcon :name="ICON.batteryLow" size="1.15em" />pile faible
  </OpBadge>
  <span v-else-if="battery" class="battery-ok" title="Pile OK (dernière trame reçue)"><OpIcon :name="ICON.battery" size="1.15em" /></span>
</template>

<style scoped>
.battery-ok{display:inline-flex}
</style>
