<script setup lang="ts">
// Onglet Reseau : Wi-Fi, MQTT et Cloud, les liens du boitier vers l'exterieur (#reseau/wifi, #reseau/mqtt,
// #reseau/cloud ; #wifi, #mqtt et #cloud y menent toujours).
import { route } from '../router';
import { SECTION } from '../domain';
import WifiSection from './network/WifiSection.vue';
import MqttSection from './network/MqttSection.vue';
import CloudSection from './network/CloudSection.vue';
</script>

<template>
  <MqttSection v-if="route.section === SECTION.mqtt" />
  <CloudSection v-else-if="route.section === SECTION.cloud" />
  <WifiSection v-else />
</template>
