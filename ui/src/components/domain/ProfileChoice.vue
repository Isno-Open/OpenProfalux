<script setup lang="ts">
// Une methode d'ajout (un profil du catalogue), en carte cliquable : la pyramide et la detection montrent la
// meme carte et menent au meme parcours. La phrase vient du champ description du profil.
import { METHOD_SENTENCE, bandLabel, type ProfileView } from '../../profiles';
import { TONE } from '../../domain';
import OpBadge from '../base/OpBadge.vue';
defineProps<{ profile: ProfileView; proposed?: boolean }>();
defineEmits<{ choose: [profile: ProfileView] }>();
</script>

<template>
  <button type="button" class="choice" :class="{ proposed }" @click="$emit('choose', profile)">
    <span class="top"><b>{{ profile.name }}</b><OpBadge v-if="profile.band">{{ bandLabel(profile.band) }}</OpBadge><OpBadge v-if="proposed" :tone="TONE.accent">proposé</OpBadge></span>
    <span class="sentence">{{ profile.description || METHOD_SENTENCE[profile.flow] }}</span>
    <span class="chevron" aria-hidden="true">›</span>
  </button>
</template>

<style scoped>
.choice{position:relative;display:grid;gap:5px;width:100%;text-align:left;padding:16px 44px 16px 18px;border:1px solid var(--border);border-radius:14px;background:var(--surface-2);color:var(--text);font:inherit;cursor:pointer;transition:.12s}
.choice:hover{border-color:var(--accent)}
.choice.proposed{border-color:var(--accent-line);background:var(--accent-weak)}
.top{display:flex;flex-wrap:wrap;align-items:center;gap:8px}
.sentence{font-size:.84rem;color:var(--muted)}
.chevron{position:absolute;right:15px;top:50%;transform:translateY(-50%);font-size:1.4rem;color:var(--muted)}
</style>
