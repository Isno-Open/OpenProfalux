<script setup>
import { computed, ref, onMounted, watch } from 'vue';
import { etat, demarrer, rafraichir } from './store.js';
import { route, aller } from './router.js';
import { ONGLETS } from './nav.js';
import IconSprite from './components/base/IconSprite.vue';
import OpTabs from './components/base/OpTabs.vue';
import OpSubtabs from './components/base/OpSubtabs.vue';
import OpToast from './components/base/OpToast.vue';
import AppHeader from './components/metier/AppHeader.vue';
import LoginScreen from './components/metier/LoginScreen.vue';
import VoletsView from './views/VoletsView.vue';
import AppareilsView from './views/AppareilsView.vue';
import ReseauView from './views/ReseauView.vue';
import OtaView from './views/OtaView.vue';
import SystemeView from './views/SystemeView.vue';
import SupportView from './views/SupportView.vue';
import AboutView from './views/AboutView.vue';

const html = document.documentElement;
const theme = ref(html.dataset.theme || 'dark');

function garder(cle, valeur) { try { localStorage.setItem(cle, valeur); } catch { /* navigation privee */ } }
function choisirTheme(t) { theme.value = t; html.dataset.theme = t; garder('opfx_theme', t); }

const onglet = computed(() => ONGLETS.find(t => t.id === route.onglet));
const sections = computed(() => (onglet.value && onglet.value.sections) || []);
const visibles = computed(() => sections.value.filter(s => !s.cache));
/* Un parcours d'ajout (learn, enrol...) allume le sous-onglet dont il part. */
const sectionActive = computed(() => {
  const s = sections.value.find(x => x.id === route.section);
  return (s && s.parent) || route.section;
});
/* Pastille sur l'onglet OTA quand une mise a jour attend. */
const onglets = computed(() => ONGLETS.map(t => (t.id === 'ota' && etat.majDispo ? { ...t, badge: 'Mise à jour disponible' } : t)));

/* Statut relu a chaque changement d'onglet ou de section, comme aujourd'hui (ex. RF debug). */
watch(() => `${route.onglet}/${route.section}`, () => rafraichir());
const heureOk = computed(() => (etat.statutOkA ? new Date(etat.statutOkA).toLocaleTimeString('fr-FR') : ''));

onMounted(demarrer);
</script>

<template>
  <IconSprite />
  <template v-if="etat.pret">
  <LoginScreen v-if="etat.protege && !etat.connecte" />
  <div v-else class="page">
    <AppHeader :theme="theme" @basculer-theme="choisirTheme(theme === 'dark' ? 'light' : 'dark')" />
    <div v-if="etat.horsLigne" class="hors-ligne" role="status">Boîtier injoignable : nouvelle tentative toutes les 3 s.<template v-if="heureOk"> Affiché : l'état de {{ heureOk }}.</template></div>
    <OpTabs :tabs="onglets" :active="route.onglet" @choisir="id => aller(id)" />
    <main class="contenu">
      <OpSubtabs v-if="visibles.length" :items="visibles" :active="sectionActive" @choisir="id => aller(`${route.onglet}/${id}`)" />
      <VoletsView v-if="route.onglet === 'control'" />
      <AppareilsView v-else-if="route.onglet === 'remotes'" />
      <ReseauView v-else-if="route.onglet === 'reseau'" />
      <OtaView v-else-if="route.onglet === 'ota'" />
      <SystemeView v-else-if="route.onglet === 'sys'" />
      <SupportView v-else-if="route.onglet === 'support'" />
      <AboutView v-else />
    </main>
  </div>
  </template>
  <OpToast />
</template>

<style scoped>
.hors-ligne{margin-top:10px;padding:9px 12px;border-radius:var(--radius-sm);background:color-mix(in srgb,var(--danger) 14%,var(--surface));border:1px solid color-mix(in srgb,var(--danger) 40%,var(--border));color:var(--text);font-size:.86rem}
</style>
