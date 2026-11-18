<script setup>
// Reseau › Wi-Fi : le reseau domestique du boitier (2,4 GHz) et son nom, qui donne son adresse .local.
import { computed, onMounted, ref } from 'vue';
import { etat, toast } from '../../store.js';
import api from '../../api/index.js';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpSwitch from '../../components/base/OpSwitch.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import SignalBars from '../../components/metier/SignalBars.vue';

const cfg = ref({});
const ssid = ref('');
const pass = ref('');
const nom = ref('');
const redemarrer = ref(true);
const fini = ref(null);
onMounted(async () => { cfg.value = await api.config(); ssid.value = cfg.value.wifi_ssid || ''; nom.value = cfg.value.device || ''; });

/* L'adresse mDNS, calculee comme le firmware (mdns_name.c) : minuscules, a-z et 0-9, tirets. */
function nettoyer(n) {
  let o = '';
  for (const c of (n || '').toLowerCase()) {
    if (o.length >= 32) break;
    if (/[a-z0-9]/.test(c)) o += c; else if (o && !o.endsWith('-')) o += '-';
  }
  return o.replace(/-+$/, '');
}
const hote = computed(() => {
  const d = nom.value.trim();
  const defaut = !d || ['openprofalux', 'volet_test'].includes(d.toLowerCase());
  return (!defaut && nettoyer(d)) || cfg.value.mdns_default || cfg.value.mdns_host || 'openprofalux';
});
const w = computed(() => (etat.statut && etat.statut.wifi) || null);
/* Le point d'acces de configuration disparait au redemarrage : dire ou retrouver le boitier. */
async function enregistrer() {
  const reseau = ssid.value.trim(), adresse = `http://${hote.value}.local`;
  const ok = await api.enregistrerConfig({ wifi_ssid: reseau, ...(pass.value ? { wifi_pass: pass.value } : {}), device: nom.value.trim(), reboot: redemarrer.value })
    .then(() => true, () => false);
  if (!ok) { toast('Échec de l\'enregistrement'); return; }
  pass.value = '';
  if (!redemarrer.value) { fini.value = null; toast('Enregistré'); return; }
  toast('Wi-Fi enregistré, redémarrage…');
  fini.value = { reseau, adresse };
}
</script>

<template>
  <OpCard title="Wi-Fi" sub="Connexion du boîtier à ton réseau (2,4 GHz : l'ESP32 ne voit pas le 5 GHz).">
    <div class="formulaire">
      <OpStatline v-if="w && w.connected" tone="ok"><b>Connecté</b> à « {{ w.ssid }} » <SignalBars :rssi="w.rssi" /> · <code>{{ w.ip }}</code></OpStatline>
      <OpStatline v-else tone="danger"><b>Non connecté</b> : le boîtier est en point d'accès de configuration. Une fois sur ton Wi-Fi, il sera à
        l'adresse <b>http://{{ hote }}.local</b></OpStatline>
      <div class="grille deux">
        <OpField label="Nom du réseau (SSID)" pour="wifi-ssid"><input id="wifi-ssid" v-model="ssid" class="op-input" autocomplete="off"></OpField>
        <OpField label="Mot de passe" pour="wifi-pass">
          <input id="wifi-pass" v-model="pass" class="op-input" type="password" placeholder="•••••••• (inchangé si vide)" autocomplete="new-password">
        </OpField>
      </div>
      <OpField label="Nom du boîtier (facultatif)" pour="wifi-nom">
        <input id="wifi-nom" v-model="nom" class="op-input" :placeholder="cfg.mdns_default || 'openprofalux'">
        <template #hint>Donne aussi son adresse sur ton réseau : <code>http://{{ hote }}.local</code></template>
      </OpField>
      <div class="option">
        <OpSwitch v-model="redemarrer">Redémarrer après enregistrement</OpSwitch>
        <p class="hint">Nécessaire pour rejoindre un nouveau réseau.</p>
      </div>
      <div class="rangee fin"><OpButton variant="primary" @click="enregistrer">Enregistrer le Wi-Fi</OpButton></div>
      <OpStatline v-if="fini" tone="ok">Après le redémarrage, reconnecte-toi à « {{ fini.reseau }} » et ouvre <a :href="`${fini.adresse}/`">{{ fini.adresse }}</a>.
        Sur Android, si l'adresse ne répond pas, utilise l'adresse IP du boîtier indiquée par ta box internet.</OpStatline>
    </div>
  </OpCard>
</template>

<style scoped>
.formulaire{display:grid;gap:14px}
.option{display:grid;gap:4px}
.option .hint{padding-left:48px}
</style>
