<script setup>
// Pile d'une telecommande (bit VLOW des trames) : rien tant qu'aucune trame n'a ete vue.
import OpIcon from '../base/OpIcon.vue';
import OpBadge from '../base/OpBadge.vue';
defineProps({ etat: { type: Object, default: null } });   // { low } ou null
</script>

<template>
  <OpBadge v-if="etat && etat.low" tone="danger" title="Bit « pile faible » vu sur plusieurs appuis d'affilée">
    <OpIcon name="pile-faible" size="1.15em" />pile faible
  </OpBadge>
  <span v-else-if="etat" class="pile-ok" title="Pile OK (dernière trame reçue)"><OpIcon name="pile" size="1.15em" /></span>
</template>

<style scoped>
.pile-ok{display:inline-flex}
</style>
