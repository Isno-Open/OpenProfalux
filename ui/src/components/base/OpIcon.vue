<script setup lang="ts">
// Une icone de la charte (voir IconSprite) : le meme dessin sur tous les appareils, contrairement aux emojis.
import type { IconName } from '../../icons';
withDefaults(defineProps<{ name: IconName; size?: string }>(), { size: '1.3em' });
</script>

<template>
  <svg class="op-icon" :style="{ width: size, height: size }" aria-hidden="true"><use :href="`#i-${name}`" /></svg>
</template>

<style scoped>
.op-icon{flex:none;vertical-align:-.28em}
</style>
