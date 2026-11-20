<script setup lang="ts">
// Reseau › MQTT : le pont vers Home Assistant. Broker (avec detection mDNS), identifiants, TLS. Les secrets
// enregistres ne sont jamais reaffiches : seule leur longueur l'est.
import { reactive, ref } from 'vue';
import { useConfig } from '../../config';
import { state, toast } from '../../store';
import api from '../../api/index';
import { navigate } from '../../router';
import type { ConfigUpdate } from '../../api/types';
import { BUTTON, SECTION, TAB, TONE } from '../../domain';
import { ICON } from '../../icons';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpSwitch from '../../components/base/OpSwitch.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import OpDetails from '../../components/base/OpDetails.vue';

const HTTP_BAD_REQUEST = 400;
const RELOAD_AFTER_REBOOT_MS = 8000;
/* Textes avec « // » : passes par une constante, vue-tsc lit mal un « // » dans un attribut statique de composant. */
const BROKER_HINT = 'mqtt://adresse, ou mqtts:// pour chiffrer (voir TLS).';
const TLS_TITLE = 'TLS (mqtts://)';
type CertKey = 'ca' | 'cert' | 'key';
const form = reactive({ uri: '', user: '', pass: '' });
const certs = reactive<Record<CertKey, string>>({ ca: '', cert: '', key: '' });
const erase = reactive<Record<CertKey, boolean>>({ ca: false, cert: false, key: false });
const discovery = ref('');
const { config, failed, load } = useConfig(c => { form.uri = c.mqtt_uri || ''; form.user = c.mqtt_user || ''; });

async function discover(): Promise<void> {
  discovery.value = 'Recherche sur le réseau…';
  const r = await api.discoverMqtt().catch(() => null);
  if (r?.uri) { form.uri = r.uri; discovery.value = `Trouvé : ${r.uri}`; } else discovery.value = 'Aucun broker trouvé.';
}
const CERTS: { key: CertKey; label: string; placeholder: string; eraseLabel: string }[] = [
  { key: 'ca', label: 'Certificat de l\'autorité (CA) du broker', placeholder: '-----BEGIN CERTIFICATE-----', eraseLabel: 'Effacer le certificat enregistré (revenir au bundle public)' },
  { key: 'cert', label: 'Certificat client (TLS mutuel)', placeholder: '-----BEGIN CERTIFICATE-----', eraseLabel: 'Effacer le certificat client enregistré' },
  { key: 'key', label: 'Clé privée client', placeholder: '-----BEGIN PRIVATE KEY-----', eraseLabel: 'Effacer la clé enregistrée' },
];
const savedLength = (key: CertKey): number => config.value?.[`mqtt_${key}_len`] ?? 0;
async function save(): Promise<void> {
  const body: ConfigUpdate = { mqtt_uri: form.uri.trim(), mqtt_user: form.user.trim() };
  if (form.pass) body.mqtt_pass = form.pass;
  /* Certificats : rempli = remplace ; « effacer » = envoie "" (supprime cote firmware) ; sinon inchange. */
  for (const { key } of CERTS) {
    if (erase[key]) body[`mqtt_${key}`] = '';
    else if (certs[key].trim()) body[`mqtt_${key}`] = certs[key].trim();
  }
  body.reboot = true;   // le pont MQTT repart sur les nouveaux reglages
  try {
    await api.saveConfig(body);
  } catch (e) {
    /* 400 = corps refuse (certificats trop volumineux) : ne pas dire « enregistre » sur un echec. */
    toast(e === HTTP_BAD_REQUEST ? 'Échec : certificats trop volumineux ou requête invalide' : 'Échec de l\'enregistrement MQTT');
    return;
  }
  form.pass = '';
  Object.assign(certs, { ca: '', cert: '', key: '' });
  Object.assign(erase, { ca: false, cert: false, key: false });
  toast('MQTT enregistré, redémarrage…');
  setTimeout(async () => { config.value = await api.config().catch(() => config.value); }, RELOAD_AFTER_REBOOT_MS);
}
const connected = (): boolean => !!state.status?.mqtt;
</script>

<template>
  <OpCard title="MQTT" sub="Le pont vers Home Assistant : le boîtier publie l'état des volets et reçoit les commandes.">
    <div class="form">
      <OpStatline :tone="connected() ? TONE.ok : TONE.danger"><b>{{ connected() ? 'Connecté' : 'Déconnecté' }}</b>
        <template v-if="connected() && config?.mqtt_uri"> · {{ config.mqtt_uri }}</template>
        <template v-else-if="!connected()"> · {{ config?.mqtt_uri ? config.mqtt_uri : 'aucun broker' }}</template></OpStatline>
      <OpField label="Broker" control="mqtt-uri" :hint="BROKER_HINT">
        <input id="mqtt-uri" v-model="form.uri" class="op-input mono" placeholder="mqtt://192.168.1.10" autocomplete="off">
        <div class="row"><OpButton small :variant="BUTTON.ghost" @click="discover">Détecter le broker (mDNS)</OpButton><span class="hint" role="status">{{ discovery }}</span></div>
      </OpField>
      <div class="grid two">
        <OpField label="Utilisateur" control="mqtt-user"><input id="mqtt-user" v-model="form.user" class="op-input" placeholder="(facultatif)" autocomplete="off"></OpField>
        <OpField label="Mot de passe" control="mqtt-pass">
          <input id="mqtt-pass" v-model="form.pass" class="op-input" type="password" autocomplete="new-password"
                 :placeholder="config?.mqtt_pass_len ? `•••••••• (${config.mqtt_pass_len} caractères enregistrés, vide = inchangé)` : 'mot de passe du broker'">
        </OpField>
      </div>
      <OpDetails :title="TLS_TITLE" :icon="ICON.system">
        <p class="hint">Préfixe le broker par <code>mqtts://</code> pour chiffrer. Colle le certificat de l'autorité du broker, obligatoire pour un broker
          auto-signé. Pour le TLS mutuel, ajoute le certificat et la clé du client. Un champ laissé vide ne change pas ce qui est enregistré.</p>
        <OpField v-for="c in CERTS" :key="c.key" :label="c.label" :control="`mqtt-${c.key}`">
          <textarea :id="`mqtt-${c.key}`" v-model="certs[c.key]" class="op-input mono" rows="3" spellcheck="false"
                    :placeholder="savedLength(c.key) ? `${savedLength(c.key)} octets enregistrés, vide = inchangé` : c.placeholder" />
          <OpSwitch v-if="savedLength(c.key)" v-model="erase[c.key]">{{ c.eraseLabel }}</OpSwitch>
        </OpField>
      </OpDetails>
      <p class="hint">Nom de l'appareil dans Home Assistant : <b>{{ config?.device || config?.mdns_default || 'OpenProfalux' }}</b>, réglé dans
        <button type="button" class="back-link" @click="navigate(`${TAB.system}/${SECTION.device}`)">Système › Appareil</button>.</p>
      <OpStatline v-if="failed" :tone="TONE.danger"><b>Configuration non lue</b> : rien ne peut être enregistré pour l'instant.
        <OpButton small @click="load">Réessayer</OpButton></OpStatline>
      <div class="row end"><OpButton :variant="BUTTON.primary" :disabled="!config" @click="save">Enregistrer le MQTT</OpButton></div>
    </div>
  </OpCard>
</template>

<style scoped>
.form{display:grid;gap:14px}
</style>
