<script setup lang="ts">
// Reseau › Wi-Fi : le reseau domestique du boitier (2,4 GHz) et son nom, qui donne son adresse .local.
import { computed, ref } from 'vue';
import { useConfig } from '../../config';
import { state, toast } from '../../store';
import api from '../../api/index';
import { BUTTON, TONE } from '../../domain';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpSwitch from '../../components/base/OpSwitch.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import SignalBars from '../../components/domain/SignalBars.vue';

const DEFAULT_HOST = 'openprofalux';
const DEFAULT_NAMES = [DEFAULT_HOST, 'volet_test'];
const MDNS_MAX = 32;
const ssid = ref('');
const pass = ref('');
const name = ref('');
const reboot = ref(true);
const done = ref<{ network: string; address: string } | null>(null);
const { config, failed, load } = useConfig(c => { ssid.value = c.wifi_ssid || ''; name.value = c.device || ''; });

/* L'adresse mDNS, calculee comme le firmware (mdns_name.c) : minuscules, a-z et 0-9, tirets. */
function sanitize(n: string): string {
  let out = '';
  for (const c of (n || '').toLowerCase()) {
    if (out.length >= MDNS_MAX) break;
    if (/[a-z0-9]/.test(c)) out += c; else if (out && !out.endsWith('-')) out += '-';
  }
  return out.replace(/-+$/, '');
}
const host = computed(() => {
  const d = name.value.trim();
  const isDefault = !d || DEFAULT_NAMES.includes(d.toLowerCase());
  return (!isDefault && sanitize(d)) || config.value?.mdns_default || config.value?.mdns_host || DEFAULT_HOST;
});
const wifi = computed(() => state.status?.wifi);
/* Le point d'acces de configuration disparait au redemarrage : dire ou retrouver le boitier. */
async function save(): Promise<void> {
  const network = ssid.value.trim(), address = `http://${host.value}.local`;
  const ok = await api.saveConfig({ wifi_ssid: network, ...(pass.value ? { wifi_pass: pass.value } : {}), device: name.value.trim(), reboot: reboot.value })
    .then(() => true, () => false);
  if (!ok) { toast('Échec de l\'enregistrement'); return; }
  pass.value = '';
  if (!reboot.value) { done.value = null; toast('Enregistré'); return; }
  toast('Wi-Fi enregistré, redémarrage…');
  done.value = { network, address };
}
</script>

<template>
  <OpCard title="Wi-Fi" sub="Connexion du boîtier à ton réseau (2,4 GHz : l'ESP32 ne voit pas le 5 GHz).">
    <div class="form">
      <OpStatline v-if="wifi?.connected" :tone="TONE.ok"><b>Connecté</b> à « {{ wifi.ssid }} » <SignalBars v-if="wifi.rssi != null" :rssi="wifi.rssi" /> · <code>{{ wifi.ip }}</code></OpStatline>
      <OpStatline v-else :tone="TONE.danger"><b>Non connecté</b> : le boîtier est en point d'accès de configuration. Une fois sur ton Wi-Fi, il sera à
        l'adresse <b>http://{{ host }}.local</b></OpStatline>
      <div class="grid two">
        <OpField label="Nom du réseau (SSID)" control="wifi-ssid"><input id="wifi-ssid" v-model="ssid" class="op-input" autocomplete="off"></OpField>
        <OpField label="Mot de passe" control="wifi-pass">
          <input id="wifi-pass" v-model="pass" class="op-input" type="password" placeholder="•••••••• (inchangé si vide)" autocomplete="new-password">
        </OpField>
      </div>
      <OpField label="Nom du boîtier (facultatif)" control="wifi-name">
        <input id="wifi-name" v-model="name" class="op-input" :placeholder="config?.mdns_default || DEFAULT_HOST">
        <template #hint>Donne aussi son adresse sur ton réseau : <code>http://{{ host }}.local</code></template>
      </OpField>
      <div class="option">
        <OpSwitch v-model="reboot">Redémarrer après enregistrement</OpSwitch>
        <p class="hint">Nécessaire pour rejoindre un nouveau réseau.</p>
      </div>
      <OpStatline v-if="failed" :tone="TONE.danger"><b>Configuration non lue</b> : rien ne peut être enregistré pour l'instant.
        <OpButton small @click="load">Réessayer</OpButton></OpStatline>
      <div class="row end"><OpButton :variant="BUTTON.primary" :disabled="!config" @click="save">Enregistrer le Wi-Fi</OpButton></div>
      <OpStatline v-if="done" :tone="TONE.ok">Après le redémarrage, reconnecte-toi à « {{ done.network }} » et ouvre <a :href="`${done.address}/`">{{ done.address }}</a>.
        Sur Android, si l'adresse ne répond pas, utilise l'adresse IP du boîtier indiquée par ta box internet.</OpStatline>
    </div>
  </OpCard>
</template>

<style scoped>
.form{display:grid;gap:14px}
.option{display:grid;gap:4px}
.option .hint{padding-left:48px}
</style>
