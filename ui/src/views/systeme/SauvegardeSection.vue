<script setup>
// Systeme › Sauvegarde : telecharger la config (appareils, telecommandes, calibration), la restaurer, et
// revenir a celle d'avant la derniere restauration (le boitier en garde une copie).
import { onMounted, ref } from 'vue';
import { rafraichir } from '../../store.js';
import api from '../../api/index.js';
import { telecharger as telechargerFichier } from '../../fichiers.js';
import OpCard from '../../components/base/OpCard.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import OpFileInput from '../../components/base/OpFileInput.vue';

const ERREURS = {
  format: 'Ce fichier n\'est pas une sauvegarde de config (l\'export des trames ?). Rien n\'a été modifié.',
  copie: 'Copie de la config actuelle impossible : restauration annulée, rien n\'a été modifié.',
  nvs: 'Config restaurée mais pas enregistrée (mémoire pleine) : elle sera perdue au redémarrage.',
  absente: 'Aucune config d\'avant à restaurer.',
};
const fichier = ref(null);
const confirmer = ref('');     // 'restaurer' | 'revenir'
const resultat = ref(null);
const avant = ref(null);
const lireAvant = async () => { avant.value = await api.avantRestauration().catch(() => null); };
onMounted(lireAvant);
const quand = t => (t > 1600000000 ? new Date(t * 1000).toLocaleString('fr-FR', { day: '2-digit', month: '2-digit', hour: '2-digit', minute: '2-digit' }) : '');

const telecharger = () => telechargerFichier('sauvegarde', 'openprofalux-backup.json', async () => JSON.stringify({ version: 1, note: 'fausses données' }, null, 2));
async function restaurer() {
  confirmer.value = '';
  const r = await api.restaurer(await fichier.value.text()).catch(() => ({}));
  resultat.value = r.ok ? { ton: 'ok', texte: 'Sauvegarde restaurée.' } : { ton: 'danger', texte: ERREURS[r.err] || 'Échec de la restauration.' };
  lireAvant(); rafraichir();
}
async function revenir() {
  confirmer.value = '';
  const r = await api.revenirAvantRestauration().catch(() => ({}));
  resultat.value = r.ok ? { ton: 'ok', texte: 'Config d\'avant restaurée. La config remplacée est gardée à sa place.' } : { ton: 'danger', texte: ERREURS[r.err] || 'Échec.' };
  lireAvant(); rafraichir();
}
</script>

<template>
  <OpCard title="Sauvegarde" sub="Télécommandes, trames de référence et calibration. À garder hors de l'ESP32 pour tout restaurer après un reset.">
    <div class="sauvegarde">
      <div class="rangee"><OpButton variant="primary" @click="telecharger">⬇ Télécharger</OpButton></div>
      <h3 class="titre-section">Restaurer</h3>
      <OpFileInput accept=".json,application/json" label="Choisir une sauvegarde (.json)" @choisi="x => { fichier = x; confirmer = ''; }" />
      <div class="rangee"><OpButton :disabled="!fichier" @click="confirmer = 'restaurer'">Restaurer (remplace la config actuelle)</OpButton></div>
      <OpStatline v-if="confirmer === 'restaurer'" tone="alerte">Remplacer toute la config actuelle par « {{ fichier.name }} » ? Le boîtier garde une copie de l'actuelle.
        <OpButton small variant="primary" @click="restaurer">Remplacer</OpButton><OpButton small @click="confirmer = ''">Annuler</OpButton></OpStatline>
      <OpStatline v-if="resultat" :tone="resultat.ton">{{ resultat.texte }}</OpStatline>
      <template v-if="avant && avant.available">
        <p class="hint">Config d'avant la dernière restauration<template v-if="quand(avant.t)"> (du {{ quand(avant.t) }})</template> :
          <OpButton small @click="confirmer = 'revenir'">↩ Revenir à cette config</OpButton></p>
        <OpStatline v-if="confirmer === 'revenir'" tone="alerte">Revenir à la config d'avant la dernière restauration ? L'actuelle sera gardée à sa place.
          <OpButton small variant="primary" @click="revenir">Revenir</OpButton><OpButton small @click="confirmer = ''">Annuler</OpButton></OpStatline>
      </template>
    </div>
  </OpCard>
</template>

<style scoped>
.sauvegarde{display:grid;gap:12px}
</style>
