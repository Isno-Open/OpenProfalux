<script setup>
// Les etapes d'un parcours guide : faites, en cours, a venir. Ne fait qu'afficher ; l'ecran decide.
defineProps({
  etapes: { type: Array, required: true },   // libelles
  actuelle: { type: Number, default: 0 },    // index de l'etape en cours (etapes.length = tout est fait)
});
</script>

<template>
  <ol class="op-steps">
    <li v-for="(e, i) in etapes" :key="e" :class="{ faite: i < actuelle, cours: i === actuelle }" :aria-current="i === actuelle ? 'step' : null">
      <span class="num">{{ i < actuelle ? '✓' : i + 1 }}</span><span class="lib">{{ e }}</span>
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
li.faite + li::before,li.faite:not(:first-child)::before{background:color-mix(in srgb,var(--ok) 55%,var(--border))}
li.cours{color:var(--text)}
li.cours .num{background:var(--accent);border-color:var(--accent);color:var(--accent-fg)}
li.faite .num{background:color-mix(in srgb,var(--ok) 18%,var(--surface));border-color:color-mix(in srgb,var(--ok) 45%,var(--border));color:var(--ok)}
.lib{font-size:.72rem;max-width:100%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
@media (max-width:520px){li:not(.cours) .lib{visibility:hidden}}
</style>
