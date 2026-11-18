<script setup>
// Onglet Appareils : la section choisie dans l'adresse (#remotes/add, #remotes/devices...), et les parcours
// d'ajout (#remotes/learn/<profil>, #remotes/enrol/<profil>, #remotes/somfy/<profil>).
import { route } from '../router.js';
import AddDevice from '../components/metier/AddDevice.vue';
import CaptureFlow from './appareils/CaptureFlow.vue';
import EnrolFlow from './appareils/EnrolFlow.vue';
import SomfyFlow from './appareils/SomfyFlow.vue';
import DevicesSection from './appareils/DevicesSection.vue';
import CentraleSection from './appareils/CentraleSection.vue';
import CalibSection from './appareils/CalibSection.vue';
import CapteesSection from './appareils/CapteesSection.vue';
</script>

<template>
  <CaptureFlow v-if="route.section === 'learn' || route.section === 'rejeu'" :key="`cap-${route.suite.join('/')}`" />
  <EnrolFlow v-else-if="route.section === 'enrol'" :key="`enr-${route.suite.join('/')}`" />
  <SomfyFlow v-else-if="route.section === 'somfy'" />
  <DevicesSection v-else-if="route.section === 'devices'" />
  <CentraleSection v-else-if="route.section === 'central'" />
  <CalibSection v-else-if="route.section === 'calib'" />
  <CapteesSection v-else-if="route.section === 'list'" />
  <AddDevice v-else />
</template>
