<script setup lang="ts">
// Support : le rapport de capture radio. On capte la telecommande ici (plusieurs pressions), on decrit le probleme,
// et le rapport part au relais. L'email sert au suivi et n'apparait jamais dans le rapport public.
import { ref } from 'vue';
import api from '../api/index';
import type { ReportBody } from '../api/types';
import { BUTTON, REPORT_PURPOSE, TONE, type Tone } from '../domain';
import { ICON } from '../icons';
import OpCard from '../components/base/OpCard.vue';
import OpField from '../components/base/OpField.vue';
import OpSelect from '../components/base/OpSelect.vue';
import OpButton from '../components/base/OpButton.vue';
import OpStatline from '../components/base/OpStatline.vue';
import CapturePanel from '../components/domain/CapturePanel.vue';

const form = ref<ReportBody>({ email: '', brand: '', model: '', purpose: REPORT_PURPOSE.debug, note: '' });
const PURPOSES = [{ value: REPORT_PURPOSE.debug, label: 'Debug (ça ne marche pas)' }, { value: REPORT_PURPOSE.newBrand, label: 'Nouvelle marque à ajouter' }];
const message = ref<{ tone: Tone; text: string } | null>(null);
const sending = ref(false);
const preview = ref('');
async function togglePreview(): Promise<void> {
  if (preview.value) { preview.value = ''; return; }
  const t = await api.lastCaptureText().catch(() => '');
  preview.value = (t && t.trim()) || '(aucune capture)';
}
async function send(): Promise<void> {
  if (!form.value.email.trim()) { message.value = { tone: TONE.warning, text: 'Mets ton email (pour le suivi).' }; return; }
  sending.value = true;
  message.value = { tone: TONE.neutral, text: 'Envoi du rapport…' };
  const d = await api.report({ ...form.value, email: form.value.email.trim() }).catch(() => null);
  sending.value = false;
  message.value = d && d.ok
    ? { tone: TONE.ok, text: 'Rapport envoyé. Tu recevras un email de suivi avec le lien de ton rapport.' }
    : { tone: TONE.danger, text: `Échec de l'envoi${d && d.status ? ` (relais ${d.status})` : ''}. Réessaie ; le relais est peut-être indisponible.` };
}
const setPurpose = (value: string | number): void => { form.value.purpose = String(value); };
</script>

<template>
  <OpCard title="Capturer et envoyer un rapport">
    <template #sub>Presse ta télécommande plusieurs fois pendant 15 s pour m'envoyer quelques trames, et j'essaierai de faire supporter ta marque
      (ou de comprendre ton bug). La capture <b>ne pilote rien</b>. Ton email sert juste au suivi, il <b>n'apparaît jamais</b> dans le rapport public.</template>
    <div class="form">
      <OpField label="Capter ta télécommande" hint="Choisis la bande, puis presse ta télécommande une ou plusieurs fois pendant 15 s. Chaque pression distincte est captée ici, sans passer par l'onglet RF.">
        <CapturePanel multi />
      </OpField>
      <div class="grid two">
        <OpField label="Ton email (pour le suivi)" control="rep-email"><input id="rep-email" v-model="form.email" class="op-input" type="email" placeholder="toi@exemple.fr" autocomplete="email"></OpField>
        <OpField label="But" control="rep-purpose"><OpSelect id="rep-purpose" :model-value="form.purpose" :options="PURPOSES" @update:model-value="setPurpose" /></OpField>
        <OpField label="Marque / modèle de moteur" control="rep-brand"><input id="rep-brand" v-model="form.brand" class="op-input" placeholder="Somfy, CAME, Nice, Velux..."></OpField>
        <OpField label="Modèle de télécommande (optionnel)" control="rep-model"><input id="rep-model" v-model="form.model" class="op-input"></OpField>
      </div>
      <OpField label="Ce que tu as fait, étape par étape" control="rep-note">
        <textarea id="rep-note" v-model="form.note" class="op-input" rows="4" placeholder="Ex. j'ai capturé en pressant le bouton montée de ma télécommande ; le portail a fait..." />
      </OpField>
      <p class="hint">Les trames captées ci-dessus (ou la dernière reçue) sont jointes automatiquement au rapport.
        <button type="button" class="back-link" @click="togglePreview">{{ preview ? 'masquer la capture jointe' : 'voir la capture jointe' }}</button></p>
      <pre v-if="preview" class="preview">{{ preview }}</pre>
      <div class="row end"><OpButton :variant="BUTTON.primary" :icon="ICON.support" :disabled="sending" @click="send">Envoyer le rapport</OpButton></div>
      <OpStatline v-if="message" :tone="message.tone">{{ message.text }}</OpStatline>
    </div>
  </OpCard>
</template>

<style scoped>
.form{display:grid;gap:14px}
.preview{margin:0;max-height:220px;overflow:auto;padding:12px;border-radius:var(--radius-sm);background:var(--surface-2);border:1px solid var(--border);font:12px/1.5 var(--mono)}
</style>
