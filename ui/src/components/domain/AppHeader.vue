<script setup lang="ts">
import { computed } from 'vue';
import { state, logout } from '../../store';
import { THEME, type Theme } from '../../domain';
import AppLogo from './AppLogo.vue';

const props = defineProps<{ theme: Theme }>();
const emit = defineEmits<{ 'toggle-theme': [] }>();
const status = computed(() => state.status);
const wifi = computed(() => status.value?.wifi);
</script>

<template>
  <header class="header">
    <div class="brand">
      <AppLogo class="header-logo" />
      <div class="titles">
        <h1>OpenProfalux <span v-if="wifi?.fallback" class="fallback"
            title="Le boîtier n'a pas pu rejoindre ton Wi-Fi. Il réessaie toutes les 30 s et redémarrera tout seul dès que le réseau répond.">(mode de secours)</span></h1>
        <div class="tag">Volets Profalux 868</div>
      </div>
    </div>
    <div class="indicators" aria-label="État du boîtier">
      <span class="pill" :class="wifi?.connected ? 'on' : 'off'"><span class="dot" />Wi-Fi</span>
      <span class="pill" :class="status?.mqtt ? 'on' : 'off'"><span class="dot" />MQTT</span>
    </div>
    <div class="actions">
      <button v-if="state.isProtected" type="button" class="icon-btn" title="Se déconnecter" aria-label="Se déconnecter" @click="logout"><svg viewBox="0 0 24 24" class="sym" aria-hidden="true"><path d="M7.5 6.3a7.5 7.5 0 1 0 9 0M12 3.5v8" /></svg></button>
      <button type="button" class="icon-btn" :title="props.theme === THEME.dark ? 'Passer en thème clair' : 'Passer en thème sombre'"
              aria-label="Thème clair / sombre" @click="emit('toggle-theme')"><svg viewBox="0 0 24 24" class="sym" aria-hidden="true"><circle cx="12" cy="12" r="8.2" /><path d="M12 3.8a8.2 8.2 0 0 1 0 16.4z" class="filled" /></svg></button>
    </div>
  </header>
</template>

<style scoped>
.header{display:flex;align-items:center;gap:14px;padding:22px 0 4px}
.brand{display:flex;align-items:center;gap:11px;margin-right:auto;min-width:0}
.header-logo{width:44px;height:44px}
h1{font-size:1.3rem;font-weight:700;letter-spacing:-.02em}
.fallback{font-size:.78rem;color:var(--warn);font-weight:600}
.tag{display:inline-block;margin-top:2px;font-size:.8rem;color:var(--muted)}
.indicators{display:flex;gap:7px}
.pill{display:inline-flex;align-items:center;gap:6px;font-size:.74rem;font-weight:600;padding:5px 10px;border-radius:999px;background:var(--surface-2);border:1px solid var(--border);color:var(--muted)}
.dot{width:7px;height:7px;border-radius:50%;background:var(--muted)}
.on{color:var(--text)}.on .dot{background:var(--ok);box-shadow:0 0 0 3px color-mix(in srgb,var(--ok) 22%,transparent)}
.off .dot{background:var(--danger)}
.actions{display:flex;gap:8px}
.icon-btn{width:36px;height:36px;display:grid;place-items:center;border:1px solid transparent;background:var(--surface);color:var(--text);border-radius:12px;cursor:pointer;font-size:1rem}
.icon-btn:hover{border-color:var(--accent-line)}
.sym{width:18px;height:18px;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round}
.sym .filled{fill:currentColor;stroke:none}
@media (max-width:520px){.indicators{display:none}}
</style>
