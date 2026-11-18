<script setup>
// Une icone de la charte (voir IconSprite) : le meme dessin sur tous les appareils, contrairement aux emojis.
defineProps({
  name: { type: String, required: true },
  size: { type: String, default: '1.3em' },
});
</script>

<template>
  <svg class="op-icon" :style="{ width: size, height: size }" aria-hidden="true"><use :href="`#i-${name}`" /></svg>
</template>

<style scoped>
.op-icon{flex:none;vertical-align:-.28em}
</style>
