<script setup lang="ts">
import { computed } from 'vue';
const props = defineProps<{ rssi: number }>();
const level = computed(() => (props.rssi >= -60 ? 4 : props.rssi >= -70 ? 3 : props.rssi >= -80 ? 2 : 1));
</script>

<template>
  <span class="signal" :title="`Signal reçu : ${rssi} dBm`">
    <span class="bars" :class="`n${level}`"><i /><i /><i /><i /></span>
    <span class="dbm">{{ rssi }} dBm</span>
  </span>
</template>

<style scoped>
.signal{display:inline-flex;align-items:center;gap:5px;white-space:nowrap}
.bars{display:inline-flex;align-items:flex-end;gap:2px;height:12px}
.bars i{width:3px;border-radius:1px;background:var(--border);display:block}
.bars i:nth-child(1){height:4px}.bars i:nth-child(2){height:6.5px}.bars i:nth-child(3){height:9px}.bars i:nth-child(4){height:12px}
.n1 i:nth-child(1){background:var(--warn)}
.n2 i:nth-child(-n+2),.n3 i:nth-child(-n+3),.n4 i:nth-child(-n+4){background:var(--accent)}
.dbm{font-family:var(--mono);font-size:.76rem;color:var(--muted);font-variant-numeric:tabular-nums}
</style>
