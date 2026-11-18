<script setup>
// Un tableau qui defile dans sa boite (trames RF, dataset). colonnes : [{ cle, label, mono, droite }].
// Chaque cellule peut etre remplacee par une fente nommee cellule-<cle>. bas : l'ecran charge la suite.
defineProps({
  colonnes: { type: Array, required: true },
  lignes: { type: Array, required: true },
  cle: { type: String, default: 'id' },
  hauteur: { type: String, default: '' },   // ex. 360px : hauteur maxi, au-dela on defile
  vide: { type: String, default: 'Rien à afficher.' },
});
const emit = defineEmits(['bas']);
function defile(e) {
  const b = e.target;
  if (b.scrollTop + b.clientHeight >= b.scrollHeight - 48) emit('bas');
}
</script>

<template>
  <div class="op-table" :style="hauteur ? { maxHeight: hauteur } : null" @scroll="defile">
    <table>
      <thead><tr><th v-for="c in colonnes" :key="c.cle" :class="{ droite: c.droite }" scope="col">{{ c.label }}</th></tr></thead>
      <tbody>
        <tr v-for="(l, i) in lignes" :key="l[cle] ?? i">
          <td v-for="c in colonnes" :key="c.cle" :class="{ mono: c.mono, droite: c.droite }">
            <slot :name="`cellule-${c.cle}`" :ligne="l">{{ l[c.cle] }}</slot>
          </td>
        </tr>
        <tr v-if="!lignes.length"><td :colspan="colonnes.length" class="vide">{{ vide }}</td></tr>
      </tbody>
    </table>
  </div>
</template>

<style scoped>
.op-table{overflow:auto;border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surface-2)}
table{width:100%;border-collapse:collapse;font-size:.84rem}
th{position:sticky;top:0;z-index:1;text-align:left;font-size:.72rem;text-transform:uppercase;letter-spacing:.05em;color:var(--muted);font-weight:600;background:var(--surface);padding:8px 10px;border-bottom:1px solid var(--border);white-space:nowrap}
td{padding:7px 10px;border-bottom:1px solid var(--border);white-space:nowrap}
tr:last-child td{border-bottom:0}
.mono{font-family:var(--mono);font-size:.8rem;color:var(--muted)}
.droite{text-align:right}
.vide{color:var(--muted);text-align:center;padding:16px}
</style>
