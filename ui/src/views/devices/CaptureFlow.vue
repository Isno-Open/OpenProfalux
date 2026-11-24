<script setup lang="ts">
// L'ecran de capture UNIFIE. Avec un profil choisi dans la pyramide (#remotes/learn/<index>) : cloner une
// telecommande Profalux (KeeLoq) ou enregistrer celle d'un portail CAME (rejeu brut) ; le profil decide le moteur,
// la radio et la frequence, le type decide les commandes a capturer. Sans profil (#remotes/learn, ou
// #remotes/learn/-/<appareil> depuis Mes appareils ou les trames RF) : gerer un appareil existant, y compris
// apprendre la telecommande d'origine d'un volet virtuel pour suivre sa position.
import { computed, ref, watch } from 'vue';
import { route, navigate } from '../../router';
import { state, withRadio, radioFor, refresh, toast, remoteName, renameFailureText } from '../../store';
import { profiles, types, typeKey, mechanism, deviceIcon, profileOf, isSomfy, hasComma, mhz } from '../../profiles';
import api from '../../api/index';
import { isOtherRemote, type CaptureRequest } from '../../api/real';
import type { Profile } from '../../api/types';
import {
  BUTTON, COMMAND, DEVICE_TYPE, FAILURE, FLOW, NEW_DEVICE, SECTION, TAB, TONE, isCommand, type Command, type Tone,
} from '../../domain';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import ChipPicker from '../../components/domain/ChipPicker.vue';
import CaptureSlots, { type CaptureAction } from '../../components/domain/CaptureSlots.vue';
import TxParamsPanel from '../../components/domain/TxParamsPanel.vue';

const SHUTTER_ACTIONS: CaptureAction[] = [{ cmd: COMMAND.up, label: 'Montée' }, { cmd: COMMAND.stop, label: 'Stop' }, { cmd: COMMAND.down, label: 'Descente' }];

/* Profil porte par l'adresse ; null = gestion d'un appareil existant. L'adresse est lue une fois : l'ecran est
   reconstruit a chaque adresse (DevicesView), et quitter l'onglet pendant une capture ne doit pas lui retirer son
   profil. */
const routeRest = [...route.rest];
const profileIndex = parseInt(routeRest[0] ?? '', 10);
const profile = computed<Profile | null>(() => (Number.isNaN(profileIndex) ? null : profiles()[profileIndex] ?? null));
const devices = computed(() => state.status?.volets ?? []);
/* Avec un profil : les appareils du meme type et du meme mecanisme (on ne clone pas un volet enrole). Sans
   profil : tous, sauf les centrales (pas de telecommande propre). */
const compatible = computed(() => devices.value.filter(d => !d.central
  && (!profile.value || (!d.zone && typeKey(d) === profile.value.type && mechanism(d) === profile.value.mecanisme))));
const preselected = routeRest[1] ? decodeURIComponent(routeRest[1]) : '';
const choice = ref<string | string[]>(preselected || (profile.value ? NEW_DEVICE : ''));
/* Depuis la pyramide, « nouvel appareil » est propose d'emblee. Le profil peut arriver apres l'ouverture (profiles.json
   en retard) : on le propose alors, tant que rien n'a ete choisi. */
watch(profile, p => { if (p && !choice.value) choice.value = NEW_DEVICE; });
const choiceId = computed(() => (typeof choice.value === 'string' ? choice.value : ''));
const name = ref('');
const id = computed(() => (choiceId.value === NEW_DEVICE ? name.value.trim() : choiceId.value));
const device = computed(() => devices.value.find(d => d.id === id.value) ?? null);
const options = computed(() => compatible.value.map(d => ({ value: d.id, label: d.id, icon: deviceIcon(d) })));
const typeKeyOf = computed(() => (profile.value ? profile.value.type : device.value ? typeKey(device.value) : DEVICE_TYPE.shutter));
const newLabel = computed(() => `+ Nouveau ${(types()[typeKeyOf.value]?.label ?? 'volet').toLowerCase()}`);

const replay = computed(() => (profile.value ? profile.value.flow === FLOW.replay : !!device.value?.rejeu));
const virtual = computed(() => !!device.value?.virt);
const somfy = computed(() => virtual.value && isSomfy(device.value));
/* Le profil qui donne radio et frequence : celui de l'adresse, sinon celui de la fiche de l'appareil. */
const effectiveProfile = computed(() => profile.value ?? (device.value ? profileOf(device.value) : null));
const actions = computed<CaptureAction[]>(() => {
  if (somfy.value) return [];
  const declared = types()[typeKeyOf.value]?.capture;
  if (!declared) return SHUTTER_ACTIONS;
  return declared.flatMap(a => (isCommand(a.cmd) ? [{ cmd: a.cmd, label: a.label }] : []));
});

const learned = computed<Partial<Record<Command, string | true>>>(() => {
  const d = device.value, out: Partial<Record<Command, string | true>> = {};
  if (!d) return out;
  for (const a of actions.value) {
    if (replay.value) { if (d.rejeu_cmds?.[a.cmd]) out[a.cmd] = true; }
    else {
      const c = d.cmd[a.cmd];
      if (c) out[a.cmd] = `0x${Number(c.b).toString(16).toUpperCase()} · ${c.s}`;
    }
  }
  return out;
});
const complete = computed(() => actions.value.length > 0 && actions.value.every(a => learned.value[a.cmd]));

/* Nom de la telecommande : prerempli avec son nom actuel quand l'appareil en a deja une (Renommer), sinon vide
   (le nom saisi est donne a la telecommande captee). Relu quand on change d'appareil. */
const remoteNameDraft = ref('');
const remoteSerial = computed(() => device.value?.serials[0] ?? '');
watch(() => `${id.value}|${remoteSerial.value}`, () => {
  remoteNameDraft.value = remoteSerial.value ? (state.status?.remotes[remoteSerial.value] || id.value) : '';
}, { immediate: true });
async function renameRemote(): Promise<void> {
  try { await api.remoteRename(remoteSerial.value, remoteNameDraft.value.trim()); toast('Télécommande renommée'); }
  catch (e) { toast(renameFailureText(e)); }
  refresh();
}

const busy = ref<Command | ''>('');
const failed = ref('');
const nameError = ref('');
const panel = ref<InstanceType<typeof TxParamsPanel> | null>(null);
/* Frequence de CETTE capture : les reglages avances d'abord. Puis, depuis la pyramide, le profil choisi ; pour un
   appareil existant, sa frequence enregistree (porteuse calee, 0 = celle de sa marque), puis sa marque. */
const frequency = computed(() => {
  const r = panel.value ? panel.value.read() : {};
  const saved = device.value?.freq_khz;
  const brand = effectiveProfile.value?.freq_khz;
  return r.freq_khz || (profile.value ? brand || saved : saved || brand) || 0;
});
/* L'enrolement n'est propose comme alternative que si la marque en declare un (pas CAME). */
const enrolment = computed(() => {
  const p = effectiveProfile.value;
  if (!p || replay.value || virtual.value) return null;
  return profiles().find(x => x.marque === p.marque && x.flow === FLOW.enrolPfx) ?? null;
});

async function capture(cmd: Command): Promise<void> {
  nameError.value = ''; failed.value = '';
  if (!id.value) { nameError.value = profile.value ? 'Donne d\'abord un nom à l\'appareil.' : 'Choisis d\'abord un appareil.'; return; }
  if (choiceId.value === NEW_DEVICE) {
    if (hasComma(id.value)) { nameError.value = 'La virgule n\'est pas permise dans un nom de volet.'; return; }
    if (device.value && !compatible.value.includes(device.value)) { nameError.value = 'Ce nom est déjà pris par un autre appareil.'; return; }
  }
  const label = actions.value.find(a => a.cmd === cmd)?.label ?? cmd;
  /* Tout ce que la capture utilise est fige ICI, avant la premiere attente : choisir un autre appareil, toucher aux
     reglages ou quitter l'onglet pendant l'attente ne doit rien changer a celle-ci ni a ce qu'elle enregistre. */
  const fromPyramid = !!profile.value, p = effectiveProfile.value, current = device.value;
  const settings = fromPyramid && panel.value ? panel.value.read() : {};
  const target = id.value, isNew = choiceId.value === NEW_DEVICE;
  const targetSerial = current?.serials[0] ?? '';
  const request: Omit<CaptureRequest, 'radio'> = {
    id: target, action: cmd, replay: replay.value, type: types()[typeKeyOf.value]?.code ?? 0, brand: p?.marque,
    freq_khz: frequency.value, rx: p?.rx, exists: !!current, remoteName: remoteSerial.value ? '' : remoteNameDraft.value.trim(),
    knownSerials: current?.serials ?? [], virtual: virtual.value,
  };
  busy.value = cmd;
  try {
    const r = await withRadio(async () => api.capture({ ...request, radio: await radioFor(p?.band) }));
    /* Les reglages avances (ajout depuis la pyramide) passent a l'appareil, comme la capture les a utilises. */
    if (fromPyramid) {
      const body: Record<string, unknown> = { id: target };
      for (const [k, v] of Object.entries(settings)) if (v) body[k] = v;
      if (p?.marque) body.marque = p.marque;
      if (Object.keys(body).length > 1) await api.saveTxParams(body).catch(() => {});
    }
    toast('n' in r ? `✓ ${label} captée (${r.n} impulsions)`
      : `✓ ${label} apprise (0x${r.button}, ${r.rssi} dBm)${r.remoteNameRefused ? ' · nom de la télécommande non enregistré : liste des télécommandes pleine' : ''}`);
    if (isNew && choiceId.value === NEW_DEVICE) choice.value = target;
    await refresh();
  } catch (e) {
    if (isOtherRemote(e)) toast(`Pas la bonne télécommande : trame de ${e.otherRemote}, le volet utilise ${targetSerial}`);
    else if (e === FAILURE.refused) toast(`Refusé par le boîtier : « ${label} » n'est pas enregistrée (liste des appareils pleine ?)`);
    else if (e !== FAILURE.busy) failed.value = label;
  } finally { busy.value = ''; }
}
async function moveCapture(from: Command, to: Command): Promise<void> {
  if (busy.value) return;   // capture en cours : sa trame ecraserait celle que l'on deplace
  try {
    await api.moveCapture({ id: id.value, from, to });
    toast(`Commande déplacée vers ${actions.value.find(a => a.cmd === to)?.label ?? to}`);
  } catch { toast('Refusé : commande non déplacée'); }
  refresh();
}

const title = computed(() => (profile.value ? profile.value.nom : 'Cloner une télécommande'));
const sub = computed(() => (!profile.value
  ? 'Tu as ta télécommande d\'origine : choisis un volet (ou crée-le), puis capture chaque commande en appuyant dessus.'
  : replay.value
    ? 'Donne un nom, puis capture en appuyant sur ta télécommande. Pour un portail, un seul bouton suffit (le moteur enchaîne la séquence).'
    : 'Donne un nom, puis capture chaque commande en appuyant sur ta télécommande. Une capture par commande suffit.'));
const instruction = computed(() => {
  if (!id.value) return 'Choisis un volet ci-dessus (ou crée-en un) pour activer la capture.';
  if (somfy.value) return `Volet Somfy « ${id.value} » : rien à capturer, il se pilote depuis l'onglet Volets.`;
  if (virtual.value) return `Volet enrôlé « ${id.value} » : pour suivre la position, clique Capturer puis appuie sur sa télécommande d'origine en tenant le bouton une seconde (moins d'un mètre du boîtier). Facultatif.`;
  return 'Clique Capturer, puis appuie sur le bouton de ta télécommande en le tenant une seconde (moins d\'un mètre du boîtier).';
});
const serialsLine = computed<{ tone: Tone; text: string } | null>(() => {
  const d = device.value;
  if (!d) return null;
  const s = d.serials;
  if (d.virt) return { tone: TONE.neutral, text: `Identité virtuelle : ${remoteName(d.virt_serial ?? '')}${s.length ? ` · Télécommandes : ${s.map(remoteName).join(', ')}` : ''}` };
  if (s.length > 1) return { tone: TONE.warning, text: `Commandes de plusieurs télécommandes : ${s.join(', ')}` };
  return s.length ? { tone: TONE.neutral, text: `Télécommande : ${s[0]}` } : null;
});
const back = computed(() => (profile.value
  ? { route: `${TAB.remotes}/${SECTION.add}/${profile.value.type}`, text: '‹ Ajouter un appareil' }
  : { route: `${TAB.remotes}/${SECTION.devices}`, text: '‹ Mes appareils' }));
const enrolmentRoute = (p: Profile): string => `${TAB.remotes}/${SECTION.enrol}/${profiles().indexOf(p)}`;
</script>

<template>
  <OpCard :title="title" :sub="sub">
    <template #before><button type="button" class="back-link" @click="navigate(back.route)">{{ back.text }}</button></template>
    <div class="flow">
      <OpStatline v-if="!replay && frequency" :tone="TONE.neutral">Capture sur la fréquence de <b>{{ profile ? (profile.marque || profile.nom) : (id || 'cet appareil') }}</b> :
        <b>{{ mhz(frequency) }}</b>.<template v-if="profile"> Ajuste-la en bas (Réglages avancés, ou « Caler sur ma télécommande »).</template></OpStatline>
      <OpField :label="profile ? `${types()[typeKeyOf]?.label ?? 'Appareil'} à compléter` : 'Volet'" :hint="profile ? 'Un appareil déjà commencé, ou un nouveau.' : ''">
        <ChipPicker v-model="choice" :options="options" :add="profile ? newLabel : ''" :disabled="!!busy" label="Appareil" empty="Aucun appareil pour l'instant." />
      </OpField>
      <OpField v-if="choiceId === NEW_DEVICE" label="Nom de l'appareil" control="cap-name" hint="Il sert d'identifiant (centrales, zones, Home Assistant). Pas de virgule.">
        <input id="cap-name" v-model="name" class="op-input" :disabled="!!busy" :placeholder="replay ? 'ex : Portail entrée' : 'ex : Salon'"
               :maxlength="state.profiles?.name_max || 23" autocomplete="off">
      </OpField>
      <OpField v-if="!replay && !somfy" label="Nom de la télécommande" control="cap-remote" :hint="remoteSerial ? '' : 'Facultatif : pour la reconnaître dans Captées.'">
        <div class="row line">
          <input id="cap-remote" v-model="remoteNameDraft" class="op-input" :placeholder="remoteSerial ? 'Nom de cette télécommande' : 'ex : Murale chambre parents'" autocomplete="off">
          <OpButton v-if="remoteSerial" @click="renameRemote">Renommer</OpButton>
        </div>
      </OpField>
      <OpStatline v-if="nameError" :tone="TONE.danger">{{ nameError }}</OpStatline>
      <OpStatline v-if="serialsLine" :tone="serialsLine.tone">{{ serialsLine.text }}</OpStatline>
      <OpStatline v-if="somfy" :tone="TONE.ok"><b>Volet Somfy RTS (télécommande virtuelle).</b> Il est piloté par son adresse et son code roulant : pas de télécommande à capturer.</OpStatline>
      <OpStatline v-else-if="virtual" :tone="TONE.ok"><b>Volet enrôlé (télécommande virtuelle).</b> Il est piloté par génération, pas besoin de cloner. En option,
        apprends ci-dessous sa <b>télécommande d'origine</b> : le boîtier suivra alors la position quand tu l'utilises.</OpStatline>

      <TxParamsPanel v-if="profile" ref="panel" :profile="profile" tune />

      <template v-if="actions.length">
        <h3 class="section-title">Capturer les commandes</h3>
        <p class="hint">{{ instruction }}</p>
        <CaptureSlots :actions="actions" :learned="learned" :busy="busy" :replay="replay" :disabled="state.radioBusy || !id"
                      @capture="capture" @move="moveCapture" />
      </template>
      <p v-else class="hint">{{ instruction }}</p>
      <OpStatline v-if="failed" :tone="TONE.danger"><b>Rien capté pour « {{ failed }} ».</b> Rapproche la télécommande à moins d'un mètre, tiens le bouton une seconde,
        et réessaie. Toujours rien ? La fréquence n'est peut-être pas la bonne : essaie <b>Caler sur ma télécommande</b>, ou vérifie le profil radio dans
        <button type="button" class="link" @click="navigate(`${TAB.system}/${SECTION.device}`)">Système › Appareil</button>.</OpStatline>
      <OpStatline v-if="complete && !virtual" :tone="TONE.ok"><b>Tout est capturé.</b> Teste depuis l'onglet Volets : le pavé
        {{ replay ? 'rejoue tes trames' : 'pilote le volet' }}.
        <OpButton small :variant="BUTTON.ghost" @click="navigate(TAB.control)">Aller aux Volets</OpButton></OpStatline>

      <OpStatline v-if="enrolment" :tone="TONE.neutral"><b>Le clonage ne pilote pas ton moteur ?</b> Certains moteurs refusent le rejeu (code roulant strict).
        Dans ce cas, <button type="button" class="link" @click="navigate(enrolmentRoute(enrolment))">crée une télécommande virtuelle</button> :
        ça nécessite aussi ta télécommande d'origine.</OpStatline>
    </div>
  </OpCard>
</template>

<style scoped>
.flow{display:grid;gap:14px}
.line{flex-wrap:nowrap}
.line .op-input{flex:1;min-width:0}
.link{background:none;border:0;padding:0;color:var(--accent);font:inherit;font-weight:600;cursor:pointer;text-decoration:underline}
</style>
