<script setup lang="ts">
withDefaults(defineProps<{ title?: string; sub?: string }>(), { title: '', sub: '' });
</script>

<template>
  <section class="op-card">
    <div v-if="$slots.before" class="before"><slot name="before" /></div>
    <header v-if="title || $slots.actions" class="head">
      <div class="titles">
        <h2 v-if="title">{{ title }}</h2>
        <p v-if="sub || $slots.sub" class="sub"><slot name="sub">{{ sub }}</slot></p>
      </div>
      <slot name="actions" />
    </header>
    <slot />
  </section>
</template>

<style scoped>
.op-card{background:var(--surface);background-image:var(--surface-grad);border:1px solid var(--border);border-radius:var(--radius);padding:var(--pad);box-shadow:var(--shadow);min-width:0}
.head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:12px}
.titles{min-width:0}
.before{margin-bottom:10px}
.sub{color:var(--muted);font-size:.85rem;margin:3px 0 0}
</style>
