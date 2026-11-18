<script setup lang="ts">
// Systeme › Etat : la sante du boitier, relue toutes les 5 s tant que l'ecran est ouvert.
import { onMounted, onUnmounted, ref } from 'vue';
import api from '../../api/index';
import type { Health } from '../../api/types';
import OpCard from '../../components/base/OpCard.vue';
import HealthPanel from '../../components/domain/HealthPanel.vue';
const REFRESH_MS = 5000;
const health = ref<Health | null>(null);
let timer: ReturnType<typeof setInterval> | undefined;
const read = async (): Promise<void> => { health.value = await api.health().catch(() => health.value); };
onMounted(() => { read(); timer = setInterval(() => { if (!document.hidden) read(); }, REFRESH_MS); });
onUnmounted(() => clearInterval(timer));
</script>

<template>
  <OpCard title="État du boîtier">
    <HealthPanel :health="health" />
    <p class="hint note">« Au plus bas » : depuis le démarrage. « Plus gros bloc » : ce qu'obtient d'un seul tenant une connexion sécurisée (GitHub, MQTT TLS).</p>
  </OpCard>
</template>

<style scoped>
.note{margin-top:12px}
</style>
