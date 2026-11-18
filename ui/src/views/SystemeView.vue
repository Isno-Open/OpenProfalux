<script setup>
// Onglet Systeme : ses six sections, choisies dans l'adresse (#sys/device, #sys/health...).
import { route } from '../router.js';
import OpCard from '../components/base/OpCard.vue';
import AppareilSection from './systeme/AppareilSection.vue';
import EtatSection from './systeme/EtatSection.vue';
import RadioSection from './systeme/RadioSection.vue';
import SauvegardeSection from './systeme/SauvegardeSection.vue';
import RfSection from './systeme/RfSection.vue';
import LogView from '../components/metier/LogView.vue';
</script>

<template>
  <EtatSection v-if="route.section === 'health'" />
  <RadioSection v-else-if="route.section === 'radio'" />
  <SauvegardeSection v-else-if="route.section === 'backup'" />
  <RfSection v-else-if="route.section === 'rf'" />
  <OpCard v-else-if="route.section === 'log'" title="Journal"
          sub="Les derniers messages du firmware, les mêmes que sur le port série : pour comprendre un échec (mise à jour, MQTT, radio) sans câble USB.">
    <LogView />
  </OpCard>
  <AppareilSection v-else />
</template>
