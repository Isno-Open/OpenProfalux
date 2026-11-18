<script setup lang="ts">
// OTA : la version installee, le retour automatique, la mise a jour depuis GitHub (le boitier telecharge lui-meme)
// ou depuis un fichier, et le retour a la version precedente. Les confirmations sont dans la page, pas des fenetres
// du navigateur. Apres un redemarrage, la page attend le boitier puis se recharge.
import { computed, onMounted, onUnmounted, ref } from 'vue';
import { state, toast } from '../store';
import api from '../api/index';
import type { OtaStatus } from '../api/types';
import type { Progress } from '../api/real-system';
import { withoutV, isNewer, updateAvailable } from '../versions';
import {
  BUTTON, CHECK_PHASE, FAILURE, FLAG, HTTP_CONFLICT, OTA_CONFIRM, OTA_IMAGE, OTA_SOURCE, TONE, isFlagOn,
  type OtaConfirm, type OtaSource, type Tone,
} from '../domain';
import { ICON } from '../icons';
import OpCard from '../components/base/OpCard.vue';
import OpButton from '../components/base/OpButton.vue';
import OpStatline from '../components/base/OpStatline.vue';
import OpSwitch from '../components/base/OpSwitch.vue';
import OpProgress from '../components/base/OpProgress.vue';
import OpFileInput from '../components/base/OpFileInput.vue';

interface Message { tone: Tone; text: string }
const TRIAL_RECHECK_MS = 65000;   // apres la minute de validation d'une version en essai
const ROLLBACK_AUTO_ON = 1, ROLLBACK_AUTO_OFF = 0;
const status = ref<OtaStatus | null>(null);
const auto = ref(true);
const check = ref<Message | null>(null);       // resultat de la verification
const checking = ref(false);
const confirm = ref<OtaConfirm>(OTA_CONFIRM.none);
const progress = ref<{ value: number | null; text: string } | null>(null);
const outcome = ref<Message | null>(null);
const file = ref<File | null>(null);
let recheck: ReturnType<typeof setTimeout> | undefined;

async function readStatus(): Promise<void> {
  status.value = await api.otaStatus().catch(() => status.value);
  /* Version en essai : relue apres la minute de validation. */
  clearTimeout(recheck);
  if (status.value?.img === OTA_IMAGE.trial) recheck = setTimeout(readStatus, TRIAL_RECHECK_MS);
}
onMounted(async () => {
  await readStatus();
  const c = await api.config().catch(() => null);
  auto.value = isFlagOn(c?.update_auto, true);   // le firmware envoie 0 / 1 ; absent (firmware d'avant) = active
  const s = status.value;
  if (s && updateAvailable(s)) check.value = { tone: TONE.warning, text: `Installée ${withoutV(s.version)}, disponible ${withoutV(s.latest)}.` };
});
onUnmounted(() => clearTimeout(recheck));

const available = computed(() => updateAvailable(status.value));
const rollbackInfo = computed<Message | null>(() => {
  const s = status.value;
  if (!s || s.rb_auto === undefined) return null;   // firmware d'avant : rien a afficher
  if (s.img === OTA_IMAGE.trial) return { tone: TONE.warning, text: 'Version en essai : validée après 1 min sans plantage, sinon retour automatique à la précédente.' };
  if (s.rb_auto === ROLLBACK_AUTO_ON) return { tone: TONE.ok, text: 'Retour automatique : actif (une mise à jour qui plante au démarrage est annulée toute seule).' };
  if (s.rb_auto === ROLLBACK_AUTO_OFF) return { tone: TONE.neutral, text: 'Retour automatique : inactif sur ce boîtier (bootloader d\'avant, voir le README du firmware pour l\'activer).' };
  return { tone: TONE.neutral, text: 'Retour automatique : vérifié à la prochaine mise à jour.' };
});
const cancelled = computed(() => (status.value?.annulee && status.value.annulee_auto ? status.value.annulee : ''));
const canRollback = computed(() => !status.value || isFlagOn(status.value.rb_possible, true));

async function checkLatest(): Promise<void> {
  checking.value = true;
  check.value = { tone: TONE.neutral, text: 'Le boîtier interroge GitHub…' };
  try {
    const s = await api.otaCheck(phase => {
      check.value = { tone: TONE.neutral, text: phase === CHECK_PHASE.waiting ? 'Le boîtier attend la fin de la connexion du cloud…' : 'Le boîtier interroge GitHub…' };
    });
    status.value = s;
    const latest = withoutV(s.latest), installed = withoutV(s.version);
    state.updateAvailable = updateAvailable(s);
    if (!latest) check.value = { tone: TONE.danger, text: 'Le boîtier n\'a pas pu lire la dernière version (Internet ? limite de l\'API GitHub ?)' };
    else if (isNewer(latest, installed)) check.value = { tone: TONE.warning, text: `Installée ${installed}, disponible ${latest}.` };
    else check.value = { tone: TONE.ok, text: installed === latest ? `${installed} = dernière version.` : `Installée ${installed}, dernière release ${latest}.` };
  } catch (e) {
    check.value = { tone: TONE.danger, text: e === FAILURE.github ? 'Pas de réponse de GitHub (Internet ?)' : 'Le boîtier ne répond pas.' };
  } finally { checking.value = false; }
}

/* Le boitier redemarre : on attend qu'il soit tombe puis revenu, puis on recharge la page (sa nouvelle interface).
   Une image refusee a la verification rejette : l'appelant affiche l'echec. */
async function waitAndReload(text: string): Promise<void> {
  outcome.value = { tone: TONE.ok, text };
  const s = await api.waitForReboot();
  outcome.value = s ? { tone: TONE.ok, text: `Redémarré sur la version ${s.version || '?'}, rechargement…` }
    : { tone: TONE.warning, text: 'Le boîtier ne répond pas encore : recharge la page dans un instant.' };
  if (s) setTimeout(() => location.reload(), 900);
}
async function install(source: OtaSource): Promise<void> {
  confirm.value = OTA_CONFIRM.none; outcome.value = null;
  const fromGithub = source === OTA_SOURCE.github;
  progress.value = { value: 0, text: fromGithub ? 'téléchargement…' : 'envoi…' };
  const onProgress: Progress = (value, text) => { progress.value = { value: value ?? progress.value?.value ?? null, text }; };
  try {
    if (fromGithub) await api.otaInstall(onProgress, status.value?.latest_url ?? '');
    else if (file.value) await api.otaUpload(file.value, onProgress);
    else return;
    progress.value = { value: 1, text: fromGithub ? 'installée' : 'reçu' };
    await waitAndReload(fromGithub ? 'Installée : redémarrage, reconnexion…' : 'Reçu : vérification et redémarrage (environ 5 s), reconnexion…');
  } catch (e) {
    progress.value = null;
    outcome.value = { tone: TONE.danger, text: `Échec${typeof e === 'string' ? ` : ${e}` : ''}. Le boîtier garde sa version.` };
  }
}
async function rollback(): Promise<void> {
  confirm.value = OTA_CONFIRM.none;
  try { await api.otaRollback(); }
  catch (e) {
    if (e === HTTP_CONFLICT && status.value) {   // refus : aucune version precedente valide (le boitier ne redemarre pas)
      status.value = { ...status.value, rb_possible: FLAG.off };
      return;
    }
  }
  try { await waitAndReload(`Retour à la version ${status.value?.precedente || 'précédente'} : redémarrage, reconnexion…`); }
  catch (e) { outcome.value = { tone: TONE.danger, text: `Échec : ${e}.` }; }
}
async function toggleAuto(on: boolean): Promise<void> {
  const r = await api.saveConfig({ update_auto: on }).catch(() => null);
  if (r && r.ok) toast(on ? 'Vérification automatique activée' : 'Vérification automatique désactivée');
  else { auto.value = !on; toast('Échec de l\'enregistrement'); }
}
const looksFull = computed(() => !!file.value && /full/i.test(file.value.name));
function onFileChosen(f: File | null): void { file.value = f; confirm.value = OTA_CONFIRM.none; }
</script>

<template>
  <OpCard title="Mise à jour (OTA)" :sub="status ? `Version courante : ${status.version}${status.part ? ` · partition ${status.part}` : ''}` : ''">
    <div class="ota">
      <OpStatline v-if="rollbackInfo" :tone="rollbackInfo.tone">{{ rollbackInfo.text }}</OpStatline>
      <OpStatline v-if="cancelled" :tone="TONE.danger">La mise à jour {{ cancelled }} n'a pas démarré correctement : le boîtier est revenu tout seul à la version précédente.</OpStatline>

      <h3 class="section-title">Depuis GitHub</h3>
      <div class="row">
        <OpButton :disabled="checking" @click="checkLatest">Vérifier la dernière version</OpButton>
        <OpButton v-if="available && status" :variant="BUTTON.primary" :icon="ICON.ota" :disabled="!!progress" @click="confirm = OTA_CONFIRM.github">Installer {{ withoutV(status.latest) }}</OpButton>
      </div>
      <OpStatline v-if="check" :tone="check.tone" role="status">{{ check.text }}</OpStatline>
      <p v-else class="hint">Le boîtier interroge GitHub et récupère la dernière release publiée.</p>
      <OpStatline v-if="confirm === OTA_CONFIRM.github" :tone="TONE.warning">Le boîtier va télécharger la mise à jour puis <b>redémarrer</b> (environ 30 s).
        <OpButton small :variant="BUTTON.primary" @click="install(OTA_SOURCE.github)">Confirmer</OpButton><OpButton small @click="confirm = OTA_CONFIRM.none">Annuler</OpButton></OpStatline>
      <div class="option">
        <OpSwitch v-model="auto" @update:model-value="toggleAuto">Vérification automatique</OpSwitch>
        <p class="hint">Le boîtier interroge GitHub toutes les 6 h (entité de mise à jour Home Assistant). Désactivée, il ne le fait que sur le bouton ci-dessus.</p>
      </div>

      <h3 class="section-title">Depuis un fichier</h3>
      <OpFileInput accept=".bin" label="Choisir le firmware (.bin)" @chosen="onFileChosen" />
      <OpStatline v-if="looksFull && file" :tone="TONE.warning">« {{ file.name }} » ressemble à un binaire <b>complet</b> (flash par câble), pas à une image OTA :
        l'OTA le refusera. Prends plutôt le fichier « -ota.bin ».</OpStatline>
      <div class="row">
        <OpButton :variant="BUTTON.primary" :disabled="!file || !!progress" @click="confirm = OTA_CONFIRM.file">Flasher</OpButton>
        <OpButton :variant="BUTTON.danger" :disabled="!canRollback || !!progress" @click="confirm = OTA_CONFIRM.rollback">↩ Version précédente</OpButton>
      </div>
      <OpStatline v-if="confirm === OTA_CONFIRM.file && file" :tone="TONE.warning">Envoyer « {{ file.name }} » au boîtier, puis redémarrer dessus ?
        <OpButton small :variant="BUTTON.primary" @click="install(OTA_SOURCE.file)">Confirmer</OpButton><OpButton small @click="confirm = OTA_CONFIRM.none">Annuler</OpButton></OpStatline>
      <OpStatline v-if="confirm === OTA_CONFIRM.rollback" :tone="TONE.warning">{{ status?.precedente ? `Revenir à la version ${status.precedente} et redémarrer ?` : 'Revenir à la version précédente et redémarrer ?' }}
        <OpButton small :variant="BUTTON.danger" @click="rollback">Revenir</OpButton><OpButton small @click="confirm = OTA_CONFIRM.none">Annuler</OpButton></OpStatline>
      <p v-if="!canRollback" class="hint">Pas de version précédente valide sur ce boîtier : rien vers quoi revenir (première installation, ou mise à jour annulée).</p>

      <div v-if="progress" class="progress"><OpProgress :value="progress.value" label="Mise à jour" /><span class="hint">{{ progress.text }}</span></div>
      <OpStatline v-if="outcome" :tone="outcome.tone">{{ outcome.text }}</OpStatline>
      <p class="hint">Écrit sur la partition inactive, puis redémarre dessus. Si la nouvelle version plante dans sa première minute, le boîtier revient tout seul à
        l'ancienne (retour automatique actif). Ne pas le débrancher pendant cette minute : il reviendrait aussi à l'ancienne version.</p>
    </div>
  </OpCard>
</template>

<style scoped>
.ota{display:grid;gap:12px}
.option{display:grid;gap:4px}
.option .hint{padding-left:48px}
.progress{display:grid;gap:6px}
</style>
