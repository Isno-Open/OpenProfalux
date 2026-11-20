<script setup lang="ts" generic="Row extends object">
// Un tableau qui defile dans sa boite (trames RF, dataset). Chaque cellule peut etre remplacee par une fente
// nommee cell-<key>. bottom : l'ecran charge la suite.
export interface Column { key: string; label: string; mono?: boolean; right?: boolean }
withDefaults(defineProps<{
  columns: Column[];
  rows: Row[];
  rowKey?: (row: Row, index: number) => string | number;
  height?: string;      // ex. 360px : hauteur maxi, au-dela on defile
  empty?: string;
}>(), { rowKey: undefined, height: '', empty: 'Rien à afficher.' });
const emit = defineEmits<{ bottom: [] }>();
function onScroll(e: Event): void {
  const box = e.target;
  if (box instanceof HTMLElement && box.scrollTop + box.clientHeight >= box.scrollHeight - 48) emit('bottom');
}
const cell = (row: Row, key: string): unknown => (key in row ? Reflect.get(row, key) : '');
</script>

<template>
  <div class="op-table" :style="height ? { maxHeight: height } : undefined" @scroll="onScroll">
    <table>
      <thead><tr><th v-for="c in columns" :key="c.key" :class="{ right: c.right }" scope="col">{{ c.label }}</th></tr></thead>
      <tbody>
        <tr v-for="(r, i) in rows" :key="rowKey ? rowKey(r, i) : i">
          <td v-for="c in columns" :key="c.key" :class="{ mono: c.mono, right: c.right }">
            <slot :name="`cell-${c.key}`" :row="r">{{ cell(r, c.key) }}</slot>
          </td>
        </tr>
        <tr v-if="!rows.length"><td :colspan="columns.length" class="empty">{{ empty }}</td></tr>
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
.right{text-align:right}
.empty{color:var(--muted);text-align:center;padding:16px}
</style>
