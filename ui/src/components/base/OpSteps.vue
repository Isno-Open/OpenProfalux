<script setup lang="ts">
// Les etapes d'un parcours guide : faites, en cours, a venir. Ne fait qu'afficher ; l'ecran decide.
withDefaults(defineProps<{
  steps: string[];      // libelles
  current?: number;     // index de l'etape en cours (steps.length = tout est fait)
}>(), { current: 0 });
</script>

<template>
  <ol class="op-steps">
    <li v-for="(s, i) in steps" :key="s" :class="{ done: i < current, current: i === current }" :aria-current="i === current ? 'step' : undefined">
      <span class="num">{{ i < current ? '✓' : i + 1 }}</span><span class="label">{{ s }}</span>
    </li>
  </ol>
</template>

<style scoped>
/* Une frise continue : numero dans un rond, trait vers l'etape suivante. Sur telephone, seul le libelle de
   l'etape en cours reste ecrit. */
.op-steps{list-style:none;margin:0;padding:0;display:flex;gap:0;flex-wrap:nowrap}
li{flex:1;position:relative;display:inline-flex;flex-direction:column;align-items:center;gap:5px;text-align:center;min-width:0;font-size:.8rem;color:var(--muted)}
.num{display:inline-grid;place-items:center;width:22px;height:22px;border-radius:50%;background:var(--surface);border:1px solid var(--border);font-weight:700;font-size:.74rem}
li:not(:first-child)::before{content:"";position:absolute;top:11px;right:calc(50% + 15px);width:calc(100% - 30px);height:2px;background:var(--border)}
li.done + li::before,li.done:not(:first-child)::before{background:color-mix(in srgb,var(--ok) 55%,var(--border))}
li.current{color:var(--text)}
li.current .num{background:var(--accent);border-color:var(--accent);color:var(--accent-fg)}
li.done .num{background:color-mix(in srgb,var(--ok) 18%,var(--surface));border-color:color-mix(in srgb,var(--ok) 45%,var(--border));color:var(--ok)}
.label{font-size:.72rem;max-width:100%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
@media (max-width:520px){li:not(.current) .label{visibility:hidden}}
</style>
