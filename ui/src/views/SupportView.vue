<script setup>
// Support : le rapport de capture radio. On capte la telecommande ici (plusieurs pressions), on decrit le
// probleme, et le rapport part au relais. L'email sert au suivi et n'apparait jamais dans le rapport public.
import { ref } from 'vue';
import api from '../api/index.js';
import OpCard from '../components/base/OpCard.vue';
import OpField from '../components/base/OpField.vue';
import OpSelect from '../components/base/OpSelect.vue';
import OpButton from '../components/base/OpButton.vue';
import OpStatline from '../components/base/OpStatline.vue';
import CapturePanel from '../components/metier/CapturePanel.vue';

const f = ref({ email: '', brand: '', model: '', purpose: 'debug', note: '' });
const statut = ref(null);
const envoi = ref(false);
const apercu = ref('');
async function voir() {
  if (apercu.value) { apercu.value = ''; return; }
  const t = await api.derniereCaptureTexte().catch(() => '');
  apercu.value = (t && t.trim()) || '(aucune capture)';
}
async function envoyer() {
  if (!f.value.email.trim()) { statut.value = { ton: 'alerte', texte: 'Mets ton email (pour le suivi).' }; return; }
  envoi.value = true;
  statut.value = { ton: 'neutre', texte: 'Envoi du rapport…' };
  const d = await api.rapport({ ...f.value, email: f.value.email.trim() }).catch(() => null);
  envoi.value = false;
  statut.value = d && d.ok
    ? { ton: 'ok', texte: 'Rapport envoyé. Tu recevras un email de suivi avec le lien de ton rapport.' }
    : { ton: 'danger', texte: `Échec de l'envoi${d && d.status ? ` (relais ${d.status})` : ''}. Réessaie ; le relais est peut-être indisponible.` };
}
</script>

<template>
  <OpCard title="Capturer et envoyer un rapport">
    <template #sub>Presse ta télécommande plusieurs fois pendant 15 s pour m'envoyer quelques trames, et j'essaierai de faire supporter ta marque
      (ou de comprendre ton bug). La capture <b>ne pilote rien</b>. Ton email sert juste au suivi, il <b>n'apparaît jamais</b> dans le rapport public.</template>
    <div class="formulaire">
      <OpField label="Capter ta télécommande" hint="Choisis la bande, puis presse ta télécommande une ou plusieurs fois pendant 15 s. Chaque pression distincte est captée ici, sans passer par l'onglet RF.">
        <CapturePanel multi />
      </OpField>
      <div class="grille deux">
        <OpField label="Ton email (pour le suivi)" pour="rep-email"><input id="rep-email" v-model="f.email" class="op-input" type="email" placeholder="toi@exemple.fr" autocomplete="email"></OpField>
        <OpField label="But" pour="rep-but"><OpSelect id="rep-but" v-model="f.purpose" :options="[{ value: 'debug', label: 'Debug (ça ne marche pas)' }, { value: 'new-brand', label: 'Nouvelle marque à ajouter' }]" /></OpField>
        <OpField label="Marque / modèle de moteur" pour="rep-marque"><input id="rep-marque" v-model="f.brand" class="op-input" placeholder="Somfy, CAME, Nice, Velux..."></OpField>
        <OpField label="Modèle de télécommande (optionnel)" pour="rep-modele"><input id="rep-modele" v-model="f.model" class="op-input"></OpField>
      </div>
      <OpField label="Ce que tu as fait, étape par étape" pour="rep-note">
        <textarea id="rep-note" v-model="f.note" class="op-input" rows="4" placeholder="Ex. j'ai capturé en pressant le bouton montée de ma télécommande ; le portail a fait..." />
      </OpField>
      <p class="hint">Les trames captées ci-dessus (ou la dernière reçue) sont jointes automatiquement au rapport.
        <button type="button" class="lien-retour" @click="voir">{{ apercu ? 'masquer la capture jointe' : 'voir la capture jointe' }}</button></p>
      <pre v-if="apercu" class="apercu">{{ apercu }}</pre>
      <div class="rangee fin"><OpButton variant="primary" icon="support" :disabled="envoi" @click="envoyer">Envoyer le rapport</OpButton></div>
      <OpStatline v-if="statut" :tone="statut.ton">{{ statut.texte }}</OpStatline>
    </div>
  </OpCard>
</template>

<style scoped>
.formulaire{display:grid;gap:14px}
.apercu{margin:0;max-height:220px;overflow:auto;padding:12px;border-radius:var(--radius-sm);background:var(--surface-2);border:1px solid var(--border);font:12px/1.5 var(--mono)}
</style>
