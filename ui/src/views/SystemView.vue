<script setup lang="ts">
// Onglet Systeme : ses six sections, choisies dans l'adresse (#sys/device, #sys/health...).
import { route } from '../router';
import { SECTION } from '../domain';
import OpCard from '../components/base/OpCard.vue';
import DeviceSection from './system/DeviceSection.vue';
import HealthSection from './system/HealthSection.vue';
import RadioSection from './system/RadioSection.vue';
import BackupSection from './system/BackupSection.vue';
import RfSection from './system/RfSection.vue';
import LogView from '../components/domain/LogView.vue';
</script>

<template>
  <HealthSection v-if="route.section === SECTION.health" />
  <RadioSection v-else-if="route.section === SECTION.radio" />
  <BackupSection v-else-if="route.section === SECTION.backup" />
  <RfSection v-else-if="route.section === SECTION.rf" />
  <OpCard v-else-if="route.section === SECTION.log" title="Journal"
          sub="Les derniers messages du firmware, les mêmes que sur le port série : pour comprendre un échec (mise à jour, MQTT, radio) sans câble USB.">
    <LogView />
  </OpCard>
  <DeviceSection v-else />
</template>
