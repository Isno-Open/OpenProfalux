<script setup lang="ts">
// Systeme › Sauvegarde : telecharger la config (appareils, telecommandes, calibration), la restaurer, et revenir a
// celle d'avant la derniere restauration (le boitier en garde une copie).
import { onMounted, ref } from 'vue';
import { refresh } from '../../store';
import api from '../../api/index';
import { download } from '../../downloads';
import type { UndoInfo } from '../../api/types';
import { BUTTON, DOWNLOAD, TONE, type Tone } from '../../domain';
import OpCard from '../../components/base/OpCard.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import OpFileInput from '../../components/base/OpFileInput.vue';

/* Raisons de refus du firmware (champ err de /api/restore). */
const RESTORE_ERRORS: Record<string, string> = {
  format: 'Ce fichier n\'est pas une sauvegarde de config (l\'export des trames ?). Rien n\'a été modifié.',
  copie: 'Copie de la config actuelle impossible : restauration annulée, rien n\'a été modifié.',
  nvs: 'Config restaurée mais pas enregistrée (mémoire pleine) : elle sera perdue au redémarrage.',
  absente: 'Aucune config d\'avant à restaurer.',
};
const CONFIRM = { none: '', restore: 'restore', undo: 'undo' } as const;
type Confirm = (typeof CONFIRM)[keyof typeof CONFIRM];
const MIN_VALID_TIME = 1600000000;   // avant : horloge pas encore synchronisee
const file = ref<File | null>(null);
const confirm = ref<Confirm>(CONFIRM.none);
const result = ref<{ tone: Tone; text: string } | null>(null);
const undo = ref<UndoInfo | null>(null);
const readUndo = async (): Promise<void> => { undo.value = await api.undoInfo().catch(() => null); };
onMounted(readUndo);
const when = (t: number | undefined): string => (t && t > MIN_VALID_TIME ? new Date(t * 1000).toLocaleString('fr-FR', { day: '2-digit', month: '2-digit', hour: '2-digit', minute: '2-digit' }) : '');

const save = (): Promise<void> => download(DOWNLOAD.backup, 'openprofalux-backup.json', () => JSON.stringify({ version: 1, note: 'fausses données' }, null, 2));
async function restore(): Promise<void> {
  confirm.value = CONFIRM.none;
  if (!file.value) return;
  const r = await api.restore(await file.value.text()).catch(() => ({ ok: 0, err: '' }));
  result.value = r.ok ? { tone: TONE.ok, text: 'Sauvegarde restaurée.' } : { tone: TONE.danger, text: RESTORE_ERRORS[r.err ?? ''] ?? 'Échec de la restauration.' };
  readUndo(); refresh();
}
async function undoRestore(): Promise<void> {
  confirm.value = CONFIRM.none;
  const r = await api.undoRestore().catch(() => ({ ok: 0, err: '' }));
  result.value = r.ok ? { tone: TONE.ok, text: 'Config d\'avant restaurée. La config remplacée est gardée à sa place.' } : { tone: TONE.danger, text: RESTORE_ERRORS[r.err ?? ''] ?? 'Échec.' };
  readUndo(); refresh();
}
function onFileChosen(f: File | null): void { file.value = f; confirm.value = CONFIRM.none; }
</script>

<template>
  <OpCard title="Sauvegarde" sub="Télécommandes, trames de référence et calibration. À garder hors de l'ESP32 pour tout restaurer après un reset.">
    <div class="backup">
      <div class="row"><OpButton :variant="BUTTON.primary" @click="save">⬇ Télécharger</OpButton></div>
      <h3 class="section-title">Restaurer</h3>
      <OpFileInput accept=".json,application/json" label="Choisir une sauvegarde (.json)" @chosen="onFileChosen" />
      <div class="row"><OpButton :disabled="!file" @click="confirm = CONFIRM.restore">Restaurer (remplace la config actuelle)</OpButton></div>
      <OpStatline v-if="confirm === CONFIRM.restore && file" :tone="TONE.warning">Remplacer toute la config actuelle par « {{ file.name }} » ? Le boîtier garde une copie de l'actuelle.
        <OpButton small :variant="BUTTON.primary" @click="restore">Remplacer</OpButton><OpButton small @click="confirm = CONFIRM.none">Annuler</OpButton></OpStatline>
      <OpStatline v-if="result" :tone="result.tone">{{ result.text }}</OpStatline>
      <template v-if="undo?.available">
        <p class="hint">Config d'avant la dernière restauration<template v-if="when(undo.t)"> (du {{ when(undo.t) }})</template> :
          <OpButton small @click="confirm = CONFIRM.undo">↩ Revenir à cette config</OpButton></p>
        <OpStatline v-if="confirm === CONFIRM.undo" :tone="TONE.warning">Revenir à la config d'avant la dernière restauration ? L'actuelle sera gardée à sa place.
          <OpButton small :variant="BUTTON.primary" @click="undoRestore">Revenir</OpButton><OpButton small @click="confirm = CONFIRM.none">Annuler</OpButton></OpStatline>
      </template>
    </div>
  </OpCard>
</template>

<style scoped>
.backup{display:grid;gap:12px}
</style>
