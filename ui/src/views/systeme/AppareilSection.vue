<script setup>
// Systeme › Appareil : nom, mot de passe de l'interface, options d'ecoute et de pilotage, puis les
// reglages de reception de chaque radio.
import { onMounted, ref } from 'vue';
import { toast, sessionFermee, protectionRetiree } from '../../store.js';
import api from '../../api/index.js';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpSwitch from '../../components/base/OpSwitch.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import RadioList from '../../components/metier/RadioList.vue';
import RadioCard from '../../components/metier/RadioCard.vue';

const cfg = ref({});
const nom = ref('');
const motDePasse = ref('');
const options = ref({ log_frames: true, mqtt_obey: true, debug: false });
const redemarrer = ref(false);
const radios = ref([]);
async function charger() {
  cfg.value = await api.config();
  nom.value = cfg.value.device || '';
  options.value = { log_frames: !!cfg.value.log_frames, mqtt_obey: cfg.value.mqtt_obey !== false, debug: !!cfg.value.debug };
  radios.value = (await api.radiosDetail()).radios || [];
}
onMounted(charger);
const OPTIONS = [
  { cle: 'log_frames', label: 'Écoute RF permanente', aide: 'Active les trames en direct, le suivi de position par la vraie télécommande et le dataset MQTT. Coupée : radio libre, moins de bruit.' },
  { cle: 'mqtt_obey', label: 'Obéir aux ordres MQTT', aide: 'Cette carte émet-elle quand Home Assistant commande un volet ? À couper sur une carte secondaire qui partage les mêmes volets : une seule émet.' },
  { cle: 'debug', label: 'Debug console', aide: 'Détaille chaque capture dans la console série. Pour diagnostiquer « ça ne capte pas ». À laisser coupé en usage normal.' },
];
/* ui_pass n'est envoye que si le champ est rempli : un champ vide ne desactive rien (il est vide par
   defaut, le mot de passe n'etant jamais reaffiche). La desactivation passe par le bouton dedie. */
async function enregistrer() {
  const nouveau = motDePasse.value !== '';
  try {
    await api.enregistrerConfig({ device: nom.value.trim(), ...options.value, ...(nouveau ? { ui_pass: motDePasse.value } : {}), reboot: redemarrer.value });
  } catch { toast('Échec de l\'enregistrement'); return; }
  motDePasse.value = '';
  if (nouveau) {
    /* Le firmware ferme toutes les sessions quand le mot de passe change : on se reconnecte. */
    toast('Mot de passe enregistré, reconnexion…');
    setTimeout(sessionFermee, 1000);
    return;
  }
  toast(redemarrer.value ? 'Enregistré, redémarrage…' : 'Enregistré');
  charger();
}
const confirmerRetrait = ref(false);
async function retirerProtection() {
  confirmerRetrait.value = false;
  await api.enregistrerConfig({ ui_pass: '' }).catch(() => {});
  toast('Protection retirée : interface ouverte');
  protectionRetiree();
  charger();
}
</script>

<template>
  <div class="grille">
    <OpCard title="Appareil">
      <div class="formulaire">
        <OpField label="Nom de l'appareil" pour="sys-nom" hint="Son nom dans Home Assistant et son adresse sur le réseau (.local).">
          <input id="sys-nom" v-model="nom" class="op-input" :placeholder="cfg.mdns_default || 'openprofalux'">
        </OpField>
        <OpField label="Mot de passe de l'interface" pour="sys-pass"
                 hint="Protège cette interface et l'API contre les autres appareils du réseau, dont l'envoi de firmware. Sans effet sur Home Assistant, qui passe par MQTT.">
          <OpStatline v-if="cfg.ui_auth" tone="ok"><b>Protection active.</b> Le mot de passe enregistré n'est jamais réaffiché : saisis-en un nouveau pour le remplacer.
            <OpButton small variant="ghost" @click="confirmerRetrait = true">Retirer la protection</OpButton></OpStatline>
          <OpStatline v-if="confirmerRetrait" tone="alerte">Retirer le mot de passe ? L'interface et l'API redeviendront accessibles à tout appareil du réseau,
            y compris pour envoyer un firmware.
            <OpButton small variant="danger" @click="retirerProtection">Retirer</OpButton><OpButton small @click="confirmerRetrait = false">Annuler</OpButton></OpStatline>
          <OpStatline v-else tone="alerte"><b>Interface ouverte</b> : tout appareil du réseau peut piloter les volets et envoyer un firmware.</OpStatline>
          <input id="sys-pass" v-model="motDePasse" class="op-input" type="password" autocomplete="new-password"
                 :placeholder="cfg.ui_auth ? 'nouveau mot de passe (vide = inchangé)' : 'choisir un mot de passe'">
        </OpField>
        <div v-for="o in OPTIONS" :key="o.cle" class="option">
          <OpSwitch v-model="options[o.cle]">{{ o.label }}</OpSwitch>
          <p class="hint">{{ o.aide }}</p>
        </div>
        <div class="rangee fin">
          <OpSwitch v-model="redemarrer">Redémarrer après</OpSwitch>
          <OpButton variant="primary" @click="enregistrer">Enregistrer</OpButton>
        </div>
      </div>
    </OpCard>

    <OpCard title="Radios" sub="La réception de chaque puce. L'émission, elle, se règle par appareil (Appareils › Mes appareils).">
      <div class="formulaire">
        <!-- Une carte a une seule radio : rien a repartir, la liste ne sert pas (comme aujourd'hui). -->
        <RadioList v-if="radios.length > 1" :radios="radios" />
        <div class="grille deux">
          <RadioCard v-for="r in radios" :key="r.idx" :radio="r" :plusieurs="radios.length > 1" @enregistre="charger" />
        </div>
      </div>
    </OpCard>
  </div>
</template>

<style scoped>
.formulaire{display:grid;gap:14px}
.option{display:grid;gap:4px}
.option .hint{padding-left:48px}
</style>
