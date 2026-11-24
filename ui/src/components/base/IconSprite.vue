<script setup lang="ts">
// Les dessins des icones, poses une seule fois dans la page ; OpIcon les reprend par <use>.
import symbols from '../../assets/icons.svg.txt?raw';
</script>

<template>
  <svg class="sprite" aria-hidden="true" v-html="symbols" />
</template>

<style scoped>
.sprite{position:absolute;width:0;height:0;overflow:hidden}
</style>
