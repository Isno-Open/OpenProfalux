<script setup lang="ts">
import { computed, ref, onMounted, watch } from 'vue';
import { state, start, refresh } from './store';
import { route, navigate } from './router';
import { TABS, type Tab } from './nav';
import { TAB, THEME, THEME_STORAGE_KEY, type SectionId, type Theme } from './domain';
import IconSprite from './components/base/IconSprite.vue';
import OpTabs from './components/base/OpTabs.vue';
import OpSubtabs from './components/base/OpSubtabs.vue';
import OpToast from './components/base/OpToast.vue';
import AppHeader from './components/domain/AppHeader.vue';
import LoginScreen from './components/domain/LoginScreen.vue';
import ShuttersView from './views/ShuttersView.vue';
import DevicesView from './views/DevicesView.vue';
import NetworkView from './views/NetworkView.vue';
import OtaView from './views/OtaView.vue';
import SystemView from './views/SystemView.vue';
import SupportView from './views/SupportView.vue';
import AboutView from './views/AboutView.vue';

const html = document.documentElement;
const theme = ref<Theme>(html.dataset.theme === THEME.light ? THEME.light : THEME.dark);

function remember(key: string, value: string): void { try { localStorage.setItem(key, value); } catch { /* navigation privee */ } }
function chooseTheme(t: Theme): void { theme.value = t; html.dataset.theme = t; remember(THEME_STORAGE_KEY, t); }

const tab = computed(() => TABS.find(t => t.id === route.tab));
const sections = computed(() => tab.value?.sections ?? []);
const visibleSections = computed(() => sections.value.filter(s => !s.hidden));
/* Un parcours d'ajout (learn, enrol...) allume le sous-onglet dont il part. */
const activeSection = computed<SectionId | null>(() => {
  const s = sections.value.find(x => x.id === route.section);
  return s?.parent ?? route.section;
});
/* Pastille sur l'onglet OTA quand une mise a jour attend. */
const tabs = computed<Tab[]>(() => TABS.map(t => (t.id === TAB.ota && state.updateAvailable ? { ...t, badge: 'Mise à jour disponible' } : t)));

/* Statut relu a chaque changement d'onglet ou de section, comme aujourd'hui (ex. RF debug). */
watch(() => `${route.tab}/${route.section}`, () => refresh());
const lastOkTime = computed(() => (state.statusOkAt ? new Date(state.statusOkAt).toLocaleTimeString('fr-FR') : ''));

onMounted(start);
</script>

<template>
  <IconSprite />
  <template v-if="state.ready">
  <LoginScreen v-if="state.isProtected && !state.loggedIn" />
  <div v-else class="page">
    <AppHeader :theme="theme" @toggle-theme="chooseTheme(theme === THEME.dark ? THEME.light : THEME.dark)" />
    <div v-if="state.status?.unsaved" class="alert" role="alert">Configuration non enregistrée : la mémoire du boîtier est pleine. Les derniers changements seront perdus au redémarrage. La place restante est dans Système, État.</div>
    <div v-if="state.offline" class="alert" role="status">Boîtier injoignable : nouvelle tentative toutes les 3 s.<template v-if="lastOkTime"> Affiché : l'état de {{ lastOkTime }}.</template></div>
    <OpTabs :tabs="tabs" :active="route.tab" @choose="id => navigate(id)" />
    <main class="content">
      <OpSubtabs v-if="visibleSections.length" :items="visibleSections" :active="activeSection" @choose="id => navigate(`${route.tab}/${id}`)" />
      <ShuttersView v-if="route.tab === TAB.control" />
      <DevicesView v-else-if="route.tab === TAB.remotes" />
      <NetworkView v-else-if="route.tab === TAB.network" />
      <OtaView v-else-if="route.tab === TAB.ota" />
      <SystemView v-else-if="route.tab === TAB.system" />
      <SupportView v-else-if="route.tab === TAB.support" />
      <AboutView v-else />
    </main>
  </div>
  </template>
  <OpToast />
</template>

<style scoped>
.alert{margin-top:10px;padding:9px 12px;border-radius:var(--radius-sm);background:color-mix(in srgb,var(--danger) 14%,var(--surface));border:1px solid color-mix(in srgb,var(--danger) 40%,var(--border));color:var(--text);font-size:.86rem}
</style>
