<script setup>
// Une ligne de « Mes appareils » : icone du type, nom, profil, et le bouton qui ouvre son edition.
import { computed } from 'vue';
import { iconeAppareil, genre, profilDe } from '../../profils.js';
import OpIcon from '../base/OpIcon.vue';
import OpBadge from '../base/OpBadge.vue';
import OpButton from '../base/OpButton.vue';
const props = defineProps({ v: { type: Object, required: true }, ouvert: Boolean });
defineEmits(['modifier']);
const g = computed(() => genre(props.v));
const profil = computed(() => (g.value.tx ? profilDe(props.v) : null));
const detail = computed(() => (props.v.central || props.v.zone ? (props.v.members || '').split(',').filter(Boolean).join(', ') || 'aucun volet' : (profil.value ? profil.value.nom : props.v.marque || '')));
</script>

<template>
  <div class="ligne" :class="{ ouvert }">
    <OpIcon :name="iconeAppareil(v)" size="1.6em" />
    <div class="texte">
      <span class="nom"><b>{{ v.id }}</b><OpBadge :tone="v.central || v.zone ? 'accent' : 'neutre'">{{ g.lbl }}</OpBadge></span>
      <span class="detail">{{ detail }}</span>
    </div>
    <OpButton small :variant="ouvert ? 'primary' : 'normal'" :aria-expanded="ouvert" @click="$emit('modifier')">{{ ouvert ? 'Fermer' : 'Modifier' }}</OpButton>
  </div>
</template>

<style scoped>
.ligne{display:grid;grid-template-columns:auto 1fr auto;align-items:center;gap:12px;padding:11px 12px;border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surface-2)}
.ligne.ouvert{border-color:var(--accent-line);border-bottom-left-radius:0;border-bottom-right-radius:0}
.texte{display:grid;gap:2px;min-width:0}
.nom{display:flex;flex-wrap:wrap;align-items:center;gap:8px}
.detail{font-size:.8rem;color:var(--muted);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
</style>
