<script setup>
// Systeme › Etat : la sante du boitier, relue toutes les 5 s tant que l'ecran est ouvert.
import { onMounted, onUnmounted, ref } from 'vue';
import api from '../../api/index.js';
import OpCard from '../../components/base/OpCard.vue';
import HealthPanel from '../../components/metier/HealthPanel.vue';
const sante = ref(null);
let minuteur = null;
const lire = async () => { sante.value = await api.sante().catch(() => sante.value); };
onMounted(() => { lire(); minuteur = setInterval(() => { if (!document.hidden) lire(); }, 5000); });
onUnmounted(() => clearInterval(minuteur));
</script>

<template>
  <OpCard title="État du boîtier">
    <HealthPanel :sante="sante" />
    <p class="hint note">« Au plus bas » : depuis le démarrage. « Plus gros bloc » : ce qu'obtient d'un seul tenant une connexion sécurisée (GitHub, MQTT TLS).</p>
  </OpCard>
</template>

<style scoped>
.note{margin-top:12px}
</style>
