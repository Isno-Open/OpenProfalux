<script setup>
// Reseau › MQTT : le pont vers Home Assistant. Broker (avec detection mDNS), identifiants, TLS. Les
// secrets enregistres ne sont jamais reaffiches : seule leur longueur l'est.
import { onMounted, ref } from 'vue';
import { etat, toast } from '../../store.js';
import api from '../../api/index.js';
import { aller } from '../../router.js';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpSwitch from '../../components/base/OpSwitch.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import OpDetails from '../../components/base/OpDetails.vue';

const cfg = ref({});
const f = ref({ uri: '', user: '', pass: '', ca: '', cert: '', key: '' });
const effacer = ref({ ca: false, cert: false, key: false });
const detection = ref('');
onMounted(async () => { cfg.value = await api.config(); f.value.uri = cfg.value.mqtt_uri || ''; f.value.user = cfg.value.mqtt_user || ''; });

async function detecter() {
  detection.value = 'Recherche sur le réseau…';
  const r = await api.mqttDecouvrir().catch(() => ({}));
  if (r.uri) { f.value.uri = r.uri; detection.value = `Trouvé : ${r.uri}`; } else detection.value = 'Aucun broker trouvé.';
}
const CERTS = [
  { cle: 'ca', label: 'Certificat de l\'autorité (CA) du broker', vide: '-----BEGIN CERTIFICATE-----', eff: 'Effacer le certificat enregistré (revenir au bundle public)' },
  { cle: 'cert', label: 'Certificat client (TLS mutuel)', vide: '-----BEGIN CERTIFICATE-----', eff: 'Effacer le certificat client enregistré' },
  { cle: 'key', label: 'Clé privée client', vide: '-----BEGIN PRIVATE KEY-----', eff: 'Effacer la clé enregistrée' },
];
const longueur = c => cfg.value[`mqtt_${c}_len`] || 0;
async function enregistrer() {
  const c = { mqtt_uri: f.value.uri.trim(), mqtt_user: f.value.user.trim() };
  if (f.value.pass) c.mqtt_pass = f.value.pass;
  /* Certificats : rempli = remplace ; « effacer » = envoie "" (supprime cote firmware) ; sinon inchange. */
  for (const { cle } of CERTS) {
    if (effacer.value[cle]) c[`mqtt_${cle}`] = '';
    else if (f.value[cle].trim()) c[`mqtt_${cle}`] = f.value[cle].trim();
  }
  c.reboot = true;   // le pont MQTT repart sur les nouveaux reglages
  try {
    await api.enregistrerConfig(c);
  } catch (e) {
    /* 400 = corps refuse (certificats trop volumineux) : ne pas dire « enregistre » sur un echec. */
    toast(e === 400 ? 'Échec : certificats trop volumineux ou requête invalide' : 'Échec de l\'enregistrement MQTT');
    return;
  }
  Object.assign(f.value, { pass: '', ca: '', cert: '', key: '' });
  effacer.value = { ca: false, cert: false, key: false };
  toast('MQTT enregistré, redémarrage…');
  setTimeout(async () => { cfg.value = await api.config().catch(() => cfg.value); }, 8000);
}
</script>

<template>
  <OpCard title="MQTT" sub="Le pont vers Home Assistant : le boîtier publie l'état des volets et reçoit les commandes.">
    <div class="formulaire">
      <OpStatline :tone="etat.statut && etat.statut.mqtt ? 'ok' : 'danger'"><b>{{ etat.statut && etat.statut.mqtt ? 'Connecté' : 'Déconnecté' }}</b>
        <template v-if="etat.statut && etat.statut.mqtt && cfg.mqtt_uri"> · {{ cfg.mqtt_uri }}</template>
        <template v-else-if="!(etat.statut && etat.statut.mqtt)"> · {{ cfg.mqtt_uri ? cfg.mqtt_uri : 'aucun broker' }}</template></OpStatline>
      <OpField label="Broker" pour="mqtt-uri" hint="mqtt://adresse, ou mqtts:// pour chiffrer (voir TLS).">
        <input id="mqtt-uri" v-model="f.uri" class="op-input mono" placeholder="mqtt://192.168.1.10" autocomplete="off">
        <div class="rangee"><OpButton small variant="ghost" @click="detecter">Détecter le broker (mDNS)</OpButton><span class="hint" role="status">{{ detection }}</span></div>
      </OpField>
      <div class="grille deux">
        <OpField label="Utilisateur" pour="mqtt-user"><input id="mqtt-user" v-model="f.user" class="op-input" placeholder="(facultatif)" autocomplete="off"></OpField>
        <OpField label="Mot de passe" pour="mqtt-pass">
          <input id="mqtt-pass" v-model="f.pass" class="op-input" type="password" autocomplete="new-password"
                 :placeholder="cfg.mqtt_pass_len ? `•••••••• (${cfg.mqtt_pass_len} caractères enregistrés, vide = inchangé)` : 'mot de passe du broker'">
        </OpField>
      </div>
      <OpDetails titre="TLS (mqtts://)" icon="sys">
        <p class="hint">Préfixe le broker par <code>mqtts://</code> pour chiffrer. Colle le certificat de l'autorité du broker, obligatoire pour un broker
          auto-signé. Pour le TLS mutuel, ajoute le certificat et la clé du client. Un champ laissé vide ne change pas ce qui est enregistré.</p>
        <OpField v-for="c in CERTS" :key="c.cle" :label="c.label" :pour="`mqtt-${c.cle}`">
          <textarea :id="`mqtt-${c.cle}`" v-model="f[c.cle]" class="op-input mono" rows="3" spellcheck="false"
                    :placeholder="longueur(c.cle) ? `${longueur(c.cle)} octets enregistrés, vide = inchangé` : c.vide" />
          <OpSwitch v-if="longueur(c.cle)" v-model="effacer[c.cle]">{{ c.eff }}</OpSwitch>
        </OpField>
      </OpDetails>
      <p class="hint">Nom de l'appareil dans Home Assistant : <b>{{ cfg.device || cfg.mdns_default || 'OpenProfalux' }}</b>, réglé dans
        <button type="button" class="lien-retour" @click="aller('sys/device')">Système › Appareil</button>.</p>
      <div class="rangee fin"><OpButton variant="primary" @click="enregistrer">Enregistrer le MQTT</OpButton></div>
    </div>
  </OpCard>
</template>

<style scoped>
.formulaire{display:grid;gap:14px}
</style>
