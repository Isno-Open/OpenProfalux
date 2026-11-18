<script setup lang="ts">
// Les radios declarees par la carte : bande, frequence d'ecoute, role, detectee ou absente. Une carte bi-radio
// dont une puce manque affiche la ligne en rouge : c'est un diagnostic.
import OpBadge from '../base/OpBadge.vue';
import { mhz } from '../../profiles';
import type { RadioDetail } from '../../api/types';
import { TONE } from '../../domain';
defineProps<{ radios: RadioDetail[] }>();
</script>

<template>
  <ul class="radios">
    <li v-for="r in radios" :key="r.idx" :class="{ missing: !r.present }">
      <span class="dot" aria-hidden="true" />
      <b>Radio {{ r.band }} MHz</b>
      <span class="meta">{{ r.freq_khz ? mhz(r.freq_khz) : '?' }}</span>
      <OpBadge :tone="r.primary ? TONE.accent : TONE.neutral">{{ r.primary ? 'primaire' : 'secondaire' }}</OpBadge>
      <OpBadge :tone="r.present ? TONE.ok : TONE.danger">{{ r.present ? 'détectée' : 'absente' }}</OpBadge>
    </li>
  </ul>
</template>

<style scoped>
.radios{list-style:none;margin:0;padding:0;display:grid;gap:7px}
li{display:flex;flex-wrap:wrap;align-items:center;gap:8px 10px;padding:9px 12px;border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surface-2)}
.dot{width:9px;height:9px;border-radius:50%;background:var(--ok);box-shadow:0 0 0 3px color-mix(in srgb,var(--ok) 22%,transparent)}
.missing{border-color:color-mix(in srgb,var(--danger) 40%,var(--border))}
.missing .dot{background:var(--danger);box-shadow:none}
.meta{font-family:var(--mono);font-size:.82rem;color:var(--muted);margin-right:auto}
</style>
