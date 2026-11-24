<script setup lang="ts">
// La barre d'onglets principale : une barre en segments sur PC, et une barre fixe en bas de l'ecran sur
// telephone (icone au-dessus du libelle).
import OpIcon from './OpIcon.vue';
import type { Tab } from '../../nav';
import type { TabId } from '../../domain';
defineProps<{ tabs: Tab[]; active: TabId }>();
defineEmits<{ choose: [id: TabId] }>();
</script>

<template>
  <nav class="op-tabs" aria-label="Onglets">
    <button v-for="t in tabs" :key="t.id" type="button" class="tab" :class="{ active: t.id === active }"
            :aria-current="t.id === active ? 'page' : undefined" @click="$emit('choose', t.id)">
      <OpIcon :name="t.icon" class="ico" />
      <span class="lbl">{{ t.label }}</span>
      <b v-if="t.badge" class="badge-dot" :title="t.badge" :aria-label="t.badge" />
    </button>
  </nav>
</template>

<style scoped>
/* Sur PC, une barre en segments ; sur telephone, une barre fixe en bas de l'ecran (icone au-dessus du
   libelle). */
.op-tabs{display:flex;flex-wrap:nowrap;gap:4px;padding:5px;margin-top:16px;background:var(--surface);border:1px solid var(--border);border-radius:16px;overflow-x:auto;scrollbar-width:none}
.tab{position:relative;flex:1;display:inline-flex;align-items:center;justify-content:center;gap:7px;padding:9px 12px;border:0;border-radius:11px;background:transparent;color:var(--muted);font-weight:550;font-size:.86rem;cursor:pointer;white-space:nowrap;transition:.14s}
.tab:hover{color:var(--text)}
.tab.active{background:var(--accent-weak);color:var(--accent);box-shadow:inset 0 0 0 1px var(--accent-line)}
.badge-dot{position:absolute;top:3px;right:5px;width:8px;height:8px;border-radius:50%;background:var(--danger);box-shadow:0 0 0 2px var(--surface)}
@media (max-width:640px){
  .op-tabs{position:fixed;z-index:20;left:0;right:0;bottom:0;margin:0;border-radius:18px 18px 0 0;border-width:1px 0 0;padding:6px 4px calc(6px + env(safe-area-inset-bottom));box-shadow:0 -8px 24px rgba(0,0,0,.25)}
  .tab{flex-direction:column;gap:2px;padding:6px 0;font-size:.6rem;letter-spacing:-.01em;min-width:0}
  .tab .ico{width:1.55em!important;height:1.55em!important}
  .tab .lbl{max-width:100%;overflow:hidden;text-overflow:ellipsis}
}
@media (max-width:380px){.tab{font-size:.55rem}}
</style>
