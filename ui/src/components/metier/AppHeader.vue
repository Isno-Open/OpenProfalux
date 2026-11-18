<script setup>
import { computed } from 'vue';
import { etat, deconnexion } from '../../store.js';
import AppLogo from './AppLogo.vue';

const props = defineProps({ theme: { type: String, required: true } });
const emit = defineEmits(['basculer-theme']);
const st = computed(() => etat.statut || {});
const wifi = computed(() => st.value.wifi || {});
</script>

<template>
  <header class="entete">
    <div class="marque">
      <AppLogo class="logo-entete" />
      <div class="titres">
        <h1>OpenProfalux <span v-if="wifi.fallback" class="secours"
            title="Le boîtier n'a pas pu rejoindre ton Wi-Fi. Il réessaie toutes les 30 s et redémarrera tout seul dès que le réseau répond.">(mode de secours)</span></h1>
        <div class="tag">Volets Profalux 868</div>
      </div>
    </div>
    <div class="etats" aria-label="État du boîtier">
      <span class="pilule" :class="wifi.connected ? 'on' : 'off'"><span class="point" />Wi-Fi</span>
      <span class="pilule" :class="st.mqtt ? 'on' : 'off'"><span class="point" />MQTT</span>
    </div>
    <div class="actions">
      <button v-if="etat.protege" type="button" class="icone" title="Se déconnecter" aria-label="Se déconnecter" @click="deconnexion"><svg viewBox="0 0 24 24" class="sym" aria-hidden="true"><path d="M7.5 6.3a7.5 7.5 0 1 0 9 0M12 3.5v8" /></svg></button>
      <button type="button" class="icone" :title="props.theme === 'dark' ? 'Passer en thème clair' : 'Passer en thème sombre'"
              aria-label="Thème clair / sombre" @click="emit('basculer-theme')"><svg viewBox="0 0 24 24" class="sym" aria-hidden="true"><circle cx="12" cy="12" r="8.2" /><path d="M12 3.8a8.2 8.2 0 0 1 0 16.4z" class="plein" /></svg></button>
    </div>
  </header>
</template>

<style scoped>
.entete{display:flex;align-items:center;gap:14px;padding:22px 0 4px}
.marque{display:flex;align-items:center;gap:11px;margin-right:auto;min-width:0}
.logo-entete{width:44px;height:44px}
h1{font-size:1.3rem;font-weight:700;letter-spacing:-.02em}
.secours{font-size:.78rem;color:var(--warn);font-weight:600}
.tag{display:inline-block;margin-top:2px;font-size:.8rem;color:var(--muted)}
.etats{display:flex;gap:7px}
.pilule{display:inline-flex;align-items:center;gap:6px;font-size:.74rem;font-weight:600;padding:5px 10px;border-radius:999px;background:var(--surface-2);border:1px solid var(--border);color:var(--muted)}
.point{width:7px;height:7px;border-radius:50%;background:var(--muted)}
.on{color:var(--text)}.on .point{background:var(--ok);box-shadow:0 0 0 3px color-mix(in srgb,var(--ok) 22%,transparent)}
.off .point{background:var(--danger)}
.actions{display:flex;gap:8px}
.icone{width:36px;height:36px;display:grid;place-items:center;border:1px solid transparent;background:var(--surface);color:var(--text);border-radius:12px;cursor:pointer;font-size:1rem}
.icone:hover{border-color:var(--accent-line)}
.sym{width:18px;height:18px;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round}
.sym .plein{fill:currentColor;stroke:none}
@media (max-width:520px){.etats{display:none}}
</style>
