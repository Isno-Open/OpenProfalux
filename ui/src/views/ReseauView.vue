<script setup>
// Onglet Reseau : Wi-Fi, MQTT et Cloud, les liens du boitier vers l'exterieur (#reseau/wifi, #reseau/mqtt,
// #reseau/cloud ; #wifi, #mqtt et #cloud y menent toujours).
import { route } from '../router.js';
import WifiSection from './reseau/WifiSection.vue';
import MqttSection from './reseau/MqttSection.vue';
import CloudSection from './reseau/CloudSection.vue';
</script>

<template>
  <MqttSection v-if="route.section === 'mqtt'" />
  <CloudSection v-else-if="route.section === 'cloud'" />
  <WifiSection v-else />
</template>
