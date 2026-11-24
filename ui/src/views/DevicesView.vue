<script setup lang="ts">
// Onglet Appareils : la section choisie dans l'adresse (#remotes/add, #remotes/devices...), et les parcours d'ajout
// (#remotes/learn/<profil>, #remotes/enrol/<profil>, #remotes/somfy/<profil>).
import { route } from '../router';
import { SECTION } from '../domain';
import AddDevice from '../components/domain/AddDevice.vue';
import CaptureFlow from './devices/CaptureFlow.vue';
import EnrolFlow from './devices/EnrolFlow.vue';
import SomfyFlow from './devices/SomfyFlow.vue';
import DeviceListSection from './devices/DeviceListSection.vue';
import CentralSection from './devices/CentralSection.vue';
import CalibrationSection from './devices/CalibrationSection.vue';
import CapturedSection from './devices/CapturedSection.vue';
</script>

<template>
  <CaptureFlow v-if="route.section === SECTION.learn || route.section === SECTION.replay" :key="`cap-${route.rest.join('/')}`" />
  <EnrolFlow v-else-if="route.section === SECTION.enrol" :key="`enr-${route.rest.join('/')}`" />
  <SomfyFlow v-else-if="route.section === SECTION.somfy" />
  <DeviceListSection v-else-if="route.section === SECTION.devices" />
  <CentralSection v-else-if="route.section === SECTION.central" />
  <CalibrationSection v-else-if="route.section === SECTION.calibration" />
  <CapturedSection v-else-if="route.section === SECTION.captured" />
  <AddDevice v-else />
</template>
