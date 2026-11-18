<script setup lang="ts">
// Systeme › Appareil : nom, mot de passe de l'interface, options d'ecoute et de pilotage, puis les reglages de
// reception de chaque radio.
import { onMounted, reactive, ref } from 'vue';
import { toast, sessionClosed, protectionRemoved } from '../../store';
import api from '../../api/index';
import type { Config, RadioDetail } from '../../api/types';
import { BUTTON, TONE, isFlagOn } from '../../domain';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpSwitch from '../../components/base/OpSwitch.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import RadioList from '../../components/domain/RadioList.vue';
import RadioCard from '../../components/domain/RadioCard.vue';

type OptionKey = 'log_frames' | 'mqtt_obey' | 'debug';
const SESSION_CLOSE_DELAY_MS = 1000;
const config = ref<Config | null>(null);
const name = ref('');
const password = ref('');
const options = reactive<Record<OptionKey, boolean>>({ log_frames: true, mqtt_obey: true, debug: false });
const reboot = ref(false);
const radios = ref<RadioDetail[]>([]);
async function load(): Promise<void> {
  const c = await api.config();
  config.value = c;
  name.value = c.device || '';
  /* Le firmware envoie 0 / 1 ; mqtt_obey absent (firmware d'avant) = oui. */
  Object.assign(options, { log_frames: isFlagOn(c.log_frames, false), mqtt_obey: isFlagOn(c.mqtt_obey, true), debug: isFlagOn(c.debug, false) });
  radios.value = (await api.radioDetails()).radios ?? [];
}
onMounted(load);
const OPTIONS: { key: OptionKey; label: string; help: string }[] = [
  { key: 'log_frames', label: 'Écoute RF permanente', help: 'Active les trames en direct, le suivi de position par la vraie télécommande et le dataset MQTT. Coupée : radio libre, moins de bruit.' },
  { key: 'mqtt_obey', label: 'Obéir aux ordres MQTT', help: 'Cette carte émet-elle quand Home Assistant commande un volet ? À couper sur une carte secondaire qui partage les mêmes volets : une seule émet.' },
  { key: 'debug', label: 'Debug console', help: 'Détaille chaque capture dans la console série. Pour diagnostiquer « ça ne capte pas ». À laisser coupé en usage normal.' },
];
/* ui_pass n'est envoye que si le champ est rempli : un champ vide ne desactive rien (il est vide par defaut, le
   mot de passe n'etant jamais reaffiche). La desactivation passe par le bouton dedie. */
async function save(): Promise<void> {
  const newPassword = password.value !== '';
  try {
    await api.saveConfig({ device: name.value.trim(), ...options, ...(newPassword ? { ui_pass: password.value } : {}), reboot: reboot.value });
  } catch { toast('Échec de l\'enregistrement'); return; }
  password.value = '';
  if (newPassword) {
    /* Le firmware ferme toutes les sessions quand le mot de passe change : on se reconnecte. */
    toast('Mot de passe enregistré, reconnexion…');
    setTimeout(sessionClosed, SESSION_CLOSE_DELAY_MS);
    return;
  }
  toast(reboot.value ? 'Enregistré, redémarrage…' : 'Enregistré');
  load();
}
const confirmRemove = ref(false);
async function removeProtection(): Promise<void> {
  confirmRemove.value = false;
  await api.saveConfig({ ui_pass: '' }).catch(() => {});
  toast('Protection retirée : interface ouverte');
  protectionRemoved();
  load();
}
</script>

<template>
  <div class="grid">
    <OpCard title="Appareil">
      <div class="form">
        <OpField label="Nom de l'appareil" control="sys-name" hint="Son nom dans Home Assistant et son adresse sur le réseau (.local).">
          <input id="sys-name" v-model="name" class="op-input" :placeholder="config?.mdns_default || 'openprofalux'">
        </OpField>
        <OpField label="Mot de passe de l'interface" control="sys-pass"
                 hint="Protège cette interface et l'API contre les autres appareils du réseau, dont l'envoi de firmware. Sans effet sur Home Assistant, qui passe par MQTT.">
          <!-- Protege ou ouvert : un seul des deux etats s'affiche. -->
          <template v-if="config?.ui_auth">
            <OpStatline :tone="TONE.ok"><b>Protection active.</b> Le mot de passe enregistré n'est jamais réaffiché : saisis-en un nouveau pour le remplacer.
              <OpButton small :variant="BUTTON.ghost" @click="confirmRemove = true">Retirer la protection</OpButton></OpStatline>
            <OpStatline v-if="confirmRemove" :tone="TONE.warning">Retirer le mot de passe ? L'interface et l'API redeviendront accessibles à tout appareil du réseau,
              y compris pour envoyer un firmware.
              <OpButton small :variant="BUTTON.danger" @click="removeProtection">Retirer</OpButton><OpButton small @click="confirmRemove = false">Annuler</OpButton></OpStatline>
          </template>
          <OpStatline v-else-if="config" :tone="TONE.warning"><b>Interface ouverte</b> : tout appareil du réseau peut piloter les volets et envoyer un firmware.</OpStatline>
          <input id="sys-pass" v-model="password" class="op-input" type="password" autocomplete="new-password"
                 :placeholder="config?.ui_auth ? 'nouveau mot de passe (vide = inchangé)' : 'choisir un mot de passe'">
        </OpField>
        <div v-for="o in OPTIONS" :key="o.key" class="option">
          <OpSwitch v-model="options[o.key]">{{ o.label }}</OpSwitch>
          <p class="hint">{{ o.help }}</p>
        </div>
        <div class="row end">
          <OpSwitch v-model="reboot">Redémarrer après</OpSwitch>
          <OpButton :variant="BUTTON.primary" @click="save">Enregistrer</OpButton>
        </div>
      </div>
    </OpCard>

    <OpCard title="Radios" sub="La réception de chaque puce. L'émission, elle, se règle par appareil (Appareils › Mes appareils).">
      <div class="form">
        <!-- Une carte a une seule radio : rien a repartir, la liste ne sert pas (comme aujourd'hui). -->
        <RadioList v-if="radios.length > 1" :radios="radios" />
        <div class="grid two">
          <RadioCard v-for="r in radios" :key="r.idx" :radio="r" :several="radios.length > 1" @saved="load" />
        </div>
      </div>
    </OpCard>
  </div>
</template>

<style scoped>
.form{display:grid;gap:14px}
.option{display:grid;gap:4px}
.option .hint{padding-left:48px}
</style>
