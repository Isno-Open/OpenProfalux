<script setup lang="ts">
// Une ligne de « Mes appareils » : icone du type, nom, profil, et le bouton qui ouvre son edition.
import { computed } from 'vue';
import { deviceIcon, kindOf, profileOf } from '../../profiles';
import type { Device } from '../../api/types';
import { BUTTON, TONE } from '../../domain';
import OpIcon from '../base/OpIcon.vue';
import OpBadge from '../base/OpBadge.vue';
import OpButton from '../base/OpButton.vue';
const props = defineProps<{ device: Device; open?: boolean }>();
defineEmits<{ edit: [] }>();
const kind = computed(() => kindOf(props.device));
const profile = computed(() => (kind.value.hasTx ? profileOf(props.device) : null));
const detail = computed(() => (props.device.central || props.device.zone
  ? (props.device.members || '').split(',').filter(Boolean).join(', ') || 'aucun volet'
  : (profile.value ? profile.value.nom : props.device.marque || '')));
</script>

<template>
  <div class="row-item" :class="{ open }">
    <OpIcon :name="deviceIcon(device)" size="1.6em" />
    <div class="text">
      <span class="name"><b>{{ device.id }}</b><OpBadge :tone="device.central || device.zone ? TONE.accent : TONE.neutral">{{ kind.label }}</OpBadge></span>
      <span class="detail">{{ detail }}</span>
    </div>
    <OpButton small :variant="open ? BUTTON.primary : BUTTON.normal" :aria-expanded="open" @click="$emit('edit')">{{ open ? 'Fermer' : 'Modifier' }}</OpButton>
  </div>
</template>

<style scoped>
.row-item{display:grid;grid-template-columns:auto 1fr auto;align-items:center;gap:12px;padding:11px 12px;border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surface-2)}
.row-item.open{border-color:var(--accent-line);border-bottom-left-radius:0;border-bottom-right-radius:0}
.text{display:grid;gap:2px;min-width:0}
.name{display:flex;flex-wrap:wrap;align-items:center;gap:8px}
.detail{font-size:.8rem;color:var(--muted);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
</style>
