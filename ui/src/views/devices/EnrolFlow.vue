<script lang="ts">
// Creer une telecommande virtuelle (enrolement) : le boitier devient une nouvelle telecommande, que le moteur
// apprend. Cinq etapes : capturer la telecommande d'origine (le boitier attribue une identite, nouvelle si le
// moteur est inconnu, la meme s'il est deja connu), emettre la trame d'apprentissage (~5 s), faire les gestes,
// tester apres le delai de confirmation du moteur (~10 s), enregistrer comme volet. L'identite vit dans le boitier
// (/api/pfx) : une identite en cours se reprend apres un F5. Le profil choisi dans la pyramide est dans l'adresse
// (#remotes/enrol/<profil>). Les gestes viennent du profil du modele choisi (catalogue, profiles[].instructions).
import { ref } from 'vue';
import type { Tone } from '../../domain';

interface Message { tone: Tone; text: string; running?: boolean; more?: string; help?: boolean }
/* L'avancement vit hors de l'ecran, qui est demonte a chaque changement d'onglet : emission faite, gestes faits et
   delai du moteur ne se refont pas pour un aller-retour, comme dans l'ancienne interface, ou le panneau restait dans
   la page. Il appartient a l'identite du boitier (owner) : une autre identite, ou plus aucune, le remet a zero. */
const owner = ref('');                    // serial de l'identite a laquelle appartient l'avancement
const captureMsg = ref<Message | null>(null);
const renewable = ref(false);
const emission = ref<Message | null>(null);
const gestures = ref(false);              // emission finie : les gestes sont a faire
const testAllowedAt = ref(0);             // instant ou le test est permis (gestes faits + delai du moteur) ; 0 = gestes pas faits
const name = ref('');
const done = ref<Message | null>(null);
</script>

<script setup lang="ts">
import { computed, onMounted, onUnmounted, watch } from 'vue';
import { route, navigate } from '../../router';
import { state, refresh, toast } from '../../store';
import { profileById, typeView, hasComma, mhz, type ProfileView } from '../../profiles';
import api from '../../api/index';
import type { PfxCaptureReply, PfxState } from '../../api/types';
import {
  BUTTON, DEVICE_TYPE, FAILURE, FLOW, NO_SERIAL, SECTION, TAB, TONE, TRISTATE, type Command,
} from '../../domain';
import { ICON } from '../../icons';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpSelect from '../../components/base/OpSelect.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import OpSteps from '../../components/base/OpSteps.vue';
import OpProgress from '../../components/base/OpProgress.vue';
import TxParamsPanel from '../../components/domain/TxParamsPanel.vue';
import ShutterControl from '../../components/domain/ShutterControl.vue';

const MOTOR_DELAY_S = 10;     // secondes de confirmation du moteur avant de tester
const CAPTURE_WINDOW_S = 15;
const EMISSION_S = 5;
/* Codes de /api/pfx/capture/poll (rc) et de /api/pfx/renew. */
const PFX_RC = { ok: 0, full: -1, nothing: -3 } as const;

const COUNTDOWN_TICK_MS = 250;
/* L'adresse est lue une fois : l'ecran est reconstruit a chaque adresse (DevicesView). */
const profileId = decodeURIComponent(route.rest[0] ?? '');
const profile = computed<ProfileView | null>(() => {
  const p = profileById(profileId);
  return p && p.flow === FLOW.enrolPfx ? p : null;
});
const pfx = ref<PfxState | null>(null);   // /api/pfx
const model = ref<string | number>(0);
const panel = ref<InstanceType<typeof TxParamsPanel> | null>(null);
const sending = ref<Command | ''>('');
/* Secondes avant le test, d'apres l'echeance : 0 = test permis ; -1 = gestes pas encore faits. */
const now = ref(Date.now());
let clock: ReturnType<typeof setInterval> | undefined;
const countdown = computed(() => (testAllowedAt.value ? Math.max(0, Math.ceil((testAllowedAt.value - now.value) / 1000)) : -1));

const models = computed(() => pfx.value?.models ?? []);
const modelOptions = computed(() => models.value.map((m, i) => ({ value: i, label: m.name })));
/* Les gestes : ceux du profil qui reconnait le modele choisi, sinon ceux du profil de l'adresse. */
const instructions = computed(() => {
  const chosen = models.value[Number(model.value)];
  return (chosen ? profileById(chosen.profile) : null)?.instructions ?? profile.value?.instructions ?? {};
});
const active = computed(() => !!pfx.value?.active);
const dpad = computed(() => typeView(DEVICE_TYPE.shutter).dpad);
const step = computed(() => (done.value?.tone === TONE.ok ? 5 : !active.value ? 0 : !gestures.value ? 1 : countdown.value !== 0 ? 2 : 3));
const identityLine = computed(() => {
  const d = pfx.value;
  if (!d || !d.active) return '';
  return `Identité ${d.serial} · slot ${(d.idx ?? 0) + 1}/63 · compteur ${d.counter}${d.remote && d.remote !== NO_SERIAL ? ` · télécommande ${d.remote}` : ''}`;
});

async function read(): Promise<void> {
  const d = await api.pfxState().catch(() => null);
  if (!d) return;
  pfx.value = d;
  if (d.active) model.value = d.model ?? 0;
  const serial = d.active ? d.serial ?? '' : '';
  if (serial !== owner.value) { owner.value = serial; gestures.value = false; testAllowedAt.value = 0; emission.value = null; }
}
onMounted(() => {
  clock = setInterval(() => { now.value = Date.now(); }, COUNTDOWN_TICK_MS);
  read();
});
/* Modele du moteur : celui de l'identite active (pose par read), sinon celui de la marque choisie dans la pyramide. Pose
   des que l'etat du boitier, le profil et le catalogue des modeles sont la (profiles.json peut arriver apres /api/pfx),
   une seule fois : ensuite, c'est le choix de l'utilisateur. */
const modelSet = ref(false);
watch([pfx, profile], () => {
  if (modelSet.value || !pfx.value) return;
  if (pfx.value.active) { modelSet.value = true; return; }
  const p = profile.value;
  if (!p || !models.value.length) return;
  const i = models.value.findIndex(m => p.models.includes(m.name));
  if (i >= 0) model.value = i;
  modelSet.value = true;
}, { immediate: true });
onUnmounted(() => clearInterval(clock));

/* Reglages du panneau -> corps de /api/pfx/capture : le firmware lit des booleens, une option absente reste
   « heriter », une frequence ou un TE a 0 n'est pas envoye. */
function captureBody(): Record<string, unknown> {
  const v = panel.value ? panel.value.read() : {}, body: Record<string, unknown> = { model: Number(model.value) };
  if (v.freq_khz) body.freq_khz = v.freq_khz;
  if (v.te_us) body.te_us = v.te_us;
  if (v.tx_rpt) body.tx_repeat_bit = v.tx_rpt === TRISTATE.yes;
  if (v.tx_rmt) body.tx_rmt = v.tx_rmt === TRISTATE.yes;
  if (profile.value?.brand) body.marque = profile.value.brand;
  return body;
}
function restart(): void { gestures.value = false; testAllowedAt.value = 0; emission.value = null; }

function captureOutcome(d: PfxCaptureReply & { used_by?: string }): Message {
  if (d.rc === PFX_RC.ok && d.new) return { tone: TONE.ok, text: `Nouveau moteur : identité ${d.serial} attribuée (télécommande ${d.remote})` };
  if (d.rc === PFX_RC.ok) {
    /* Identite reprise : proposer une identite neuve, au cas ou d'autres moteurs la connaissent (issue #53). */
    renewable.value = true;
    return { tone: TONE.ok, text: `Moteur déjà connu : même identité ${d.serial} (télécommande ${d.remote})`,
      more: d.used_by ? `Cette identité pilote déjà le volet virtuel « ${d.used_by} » : tout moteur qui la connaît bouge avec lui.` : '' };
  }
  if (d.rc === PFX_RC.full) return { tone: TONE.danger, text: '63 identités déjà attribuées : oublie-en une pour libérer un slot.' };
  /* Capture ratee : l'identite d'avant est deselectionnee, rien ne partira avec elle (issue #53). */
  if (d.rc === PFX_RC.nothing) return { tone: TONE.danger, text: 'La capture a échoué : le boîtier n\'a rien reçu. Rapproche la télécommande du boîtier (moins d\'un mètre), tiens le bouton une seconde, et recommence.', help: true };
  return { tone: TONE.danger, text: 'La capture a échoué : télécommande non reconnue. Recommence avec la télécommande de ce volet.' };
}
async function capture(): Promise<void> {
  if (state.radioBusy) { toast('Radio occupée, réessaie dans un instant'); return; }
  state.radioBusy = true;
  renewable.value = false;
  restart();
  let n = CAPTURE_WINDOW_S;
  const text = (): string => `Appuie MAINTENANT sur ta télécommande d'origine… ${n} s`;
  captureMsg.value = { tone: TONE.neutral, text: text(), running: true };
  const ticker = setInterval(() => { n = Math.max(0, n - 1); if (captureMsg.value) captureMsg.value = { ...captureMsg.value, text: text() }; }, 1000);
  try {
    const d = await api.pfxCapture(captureBody());
    captureMsg.value = captureOutcome(d);
  } catch (e) {
    captureMsg.value = { tone: TONE.danger, text: e === FAILURE.start ? 'Échec du démarrage de la capture' : 'Aucune réponse du boîtier' };
  } finally { clearInterval(ticker); state.radioBusy = false; }
  await read();
}
async function renew(): Promise<void> {
  const r = await api.pfxRenew().catch(() => null);
  if (r && r.ok) {
    renewable.value = false;
    captureMsg.value = { tone: TONE.ok, text: `Identité neuve ${r.serial} attribuée : l'ancienne reste réservée, aucun autre moteur ne la recevra.` };
    restart();
  } else if (r && r.rc === PFX_RC.full) captureMsg.value = { tone: TONE.danger, text: '63 identités déjà attribuées : plus de slot libre pour une identité neuve.' };
  else captureMsg.value = { tone: TONE.danger, text: 'Refusé : refais la capture de la télécommande, puis recommence.' };
  await read();
}
async function sendLearnFrame(): Promise<void> {
  if (state.radioBusy) { toast('Radio occupée, réessaie dans un instant'); return; }
  state.radioBusy = true;
  gestures.value = false; testAllowedAt.value = 0;
  let n = EMISSION_S;
  const text = (): string => `Émission ~5 s : ne touche à rien… ${n} s`;
  emission.value = { tone: TONE.neutral, text: text(), running: true };
  const ticker = setInterval(() => { n = Math.max(0, n - 1); if (emission.value) emission.value = { ...emission.value, text: text() }; }, 1000);
  try {
    const r = await api.pfxLearn();
    if (r && r.ok) {
      emission.value = { tone: TONE.ok, text: 'Émission finie. Fais les gestes ci-dessous MAINTENANT, puis clique « J\'ai fait les gestes ».' };
      gestures.value = true;   // les gestes ne se montrent qu'a la fin de l'emission
    } else emission.value = { tone: TONE.danger, text: r && r.err ? r.err : 'Échec (radio occupée ?)' };
  } catch { emission.value = { tone: TONE.danger, text: 'Radio occupée, réessaie dans un instant.' }; }
  finally { clearInterval(ticker); state.radioBusy = false; await read(); }
}
/* Gestes faits : le delai de confirmation du moteur (~10 s) passe avant d'autoriser le test. Une echeance, et non un
   compte a rebours de l'ecran : elle court aussi pendant un passage sur un autre onglet. */
function gesturesDone(): void {
  now.value = Date.now();
  testAllowedAt.value = now.value + MOTOR_DELAY_S * 1000;
}
async function test(cmd: Command): Promise<void> {
  if (countdown.value !== 0 || state.radioBusy) return;
  state.radioBusy = true; sending.value = cmd;
  try { await api.pfxCommand(cmd); } catch { toast('Radio occupée, réessaie dans un instant'); }
  finally { sending.value = ''; state.radioBusy = false; read(); }
}
async function save(): Promise<void> {
  const n = name.value.trim();
  if (!n) { toast('Donne un nom au volet'); return; }
  if (hasComma(n)) { toast('La virgule n\'est pas permise dans un nom de volet'); return; }
  try {
    await api.pfxSave(n);
    toast('Volet créé : voir l\'onglet Volets');
    done.value = { tone: TONE.ok, text: `Volet « ${n} » enregistré. Il est dans l'onglet Volets et dans Home Assistant.` };
    name.value = '';
    await read();   // l'identite a migre vers le volet
    refresh();
  } catch { toast('Échec de l\'enregistrement'); }
}
async function forget(): Promise<void> {
  await api.pfxForget().catch(() => {});
  toast('Identité désélectionnée · compteur conservé');
  restart(); captureMsg.value = null; done.value = null;
  await read();
}
const subtitle = computed(() => (profile.value ? `${profile.value.name}${profile.value.freq_khz ? ` · ${mhz(profile.value.freq_khz)}` : ''}` : ''));
</script>

<template>
  <OpCard title="Créer une télécommande virtuelle" :sub="subtitle">
    <template #before><button type="button" class="back-link" @click="navigate(`${TAB.remotes}/${SECTION.add}/${DEVICE_TYPE.shutter}`)">‹ Ajouter un appareil</button></template>
    <div class="flow">
      <p class="hint">À utiliser si le clonage ne pilote pas ton moteur. On crée une nouvelle télécommande virtuelle et on l'apprend au moteur.
        <b>Nécessite ta télécommande d'origine</b> (pour le geste de validation). Appuie dessus : le boîtier l'identifie et attribue une identité,
        <b>nouvelle</b> si le moteur est inconnu, <b>la même</b> s'il est déjà connu.</p>
      <OpSteps :steps="['Capturer', 'Émettre', 'Gestes', 'Tester', 'Enregistrer']" :current="step" />

      <!-- 1. Capturer la telecommande d'origine : le boitier attribue l'identite -->
      <section class="step" :class="{ done: step > 0 }">
        <h3 class="section-title">Identifier le moteur</h3>
        <OpField label="Modèle de moteur" control="pfx-model">
          <OpSelect id="pfx-model" v-model="model" :options="modelOptions" @update:model-value="modelSet = true" />
        </OpField>
        <TxParamsPanel ref="panel" :profile="profile" tune />
        <div class="row">
          <OpButton :variant="active ? BUTTON.normal : BUTTON.primary" :icon="ICON.remote" :disabled="state.radioBusy" @click="capture">{{ active ? 'Recapturer' : 'Capturer' }}</OpButton>
        </div>
        <OpProgress v-if="captureMsg?.running" label="Capture en cours" />
        <OpStatline v-if="captureMsg" :tone="captureMsg.tone">{{ captureMsg.text }}<template v-if="captureMsg.more"><br>⚠️ {{ captureMsg.more }}</template>
          <template v-if="captureMsg.help"><br>Toujours rien ? Vérifie le profil radio dans
            <button type="button" class="link" @click="navigate(`${TAB.system}/${SECTION.device}`)">Système › Appareil</button>.</template></OpStatline>
        <template v-if="renewable">
          <p class="hint">Tu réenrôles ce moteur : continue avec cette identité. Si elle a été apprise par <b>d'autres moteurs</b> (ils bougent ensemble),
            crée plutôt une identité neuve : elle ne servira qu'au moteur que tu enrôles maintenant.</p>
          <div class="row"><OpButton @click="renew">🆕 Créer une identité neuve à la place</OpButton></div>
        </template>
        <OpStatline v-if="identityLine" :tone="TONE.ok"><b>{{ identityLine }}</b></OpStatline>
      </section>

      <template v-if="active">
        <!-- 2. Emettre la trame d'apprentissage -->
        <section class="step" :class="{ done: gestures }">
          <h3 class="section-title">1. Enrôler l'identité sur le moteur</h3>
          <OpStatline v-if="instructions.before" :tone="TONE.warning"><b>{{ instructions.before }}</b></OpStatline>
          <p class="hint">Clique ci-dessous : l'émission dure ~5 s. <b>Ne touche à rien</b> pendant. Les gestes à faire sur ta télécommande s'afficheront
            <b>à la fin</b> de l'émission.</p>
          <div class="row"><OpButton :variant="gestures ? BUTTON.normal : BUTTON.primary" :icon="ICON.remote" :disabled="state.radioBusy || !pfx?.learn_ok"
                                     :title="pfx?.learn_ok ? '' : 'Capte d\'abord la télécommande de ce volet'" @click="sendLearnFrame">📡 Émettre la trame d'apprentissage (~5 s)</OpButton></div>
          <OpProgress v-if="emission?.running" label="Émission en cours" />
          <OpStatline v-if="emission" :tone="emission.tone">{{ emission.text }}</OpStatline>
          <template v-if="gestures">
            <p class="hint"><b>✅ Émission finie. Fais MAINTENANT sur ta télécommande d'origine :</b></p>
            <ol class="gestures"><li v-for="g in instructions.gestures ?? []" :key="g">{{ g }}</li></ol>
            <p class="hint">Quand les gestes sont faits, clique ci-dessous. Beaucoup de moteurs font un petit va-et-vient pour confirmer : bon signe.
              Certains ne bougent pas du tout : c'est normal aussi.</p>
            <div v-if="countdown < 0" class="row"><OpButton :variant="BUTTON.primary" @click="gesturesDone">✅ J'ai fait les gestes</OpButton></div>
            <OpStatline v-else-if="countdown > 0" :tone="TONE.neutral">Le moteur confirme, patiente… {{ countdown }} s</OpStatline>
            <OpStatline v-else :tone="TONE.ok">Tu peux tester maintenant avec ▲ ■ ▼.</OpStatline>
          </template>
        </section>

        <!-- 3. Tester, puis enregistrer -->
        <template v-if="countdown === 0">
          <section class="step" :class="{ done: step > 3 }">
            <h3 class="section-title">2. Tester la nouvelle identité</h3>
            <p class="hint">Si le moteur a bien appris, ces boutons doivent le piloter. S'il ne répond pas tout de suite, réessaie : ne recommence pas l'enrôlement.</p>
            <ShutterControl class="test-pad" :labels="dpad" :sending="sending" :disabled="state.radioBusy" @command="test" />
          </section>
          <section class="step">
            <h3 class="section-title">3. Enregistrer comme volet</h3>
            <p class="hint">Pour le retrouver dans l'onglet <b>Volets</b> et le piloter depuis Home Assistant.</p>
            <div class="row line">
              <input v-model="name" class="op-input" placeholder="Nom du volet (ex : Salon)" :maxlength="state.profiles?.name_max || 23" aria-label="Nom du volet">
              <OpButton :variant="BUTTON.primary" @click="save">💾 Enregistrer</OpButton>
            </div>
          </section>
        </template>
        <div class="row end"><OpButton :variant="BUTTON.ghost" small @click="forget">↩ Désélectionner (garde l'identité et son compteur)</OpButton></div>
      </template>
      <OpStatline v-if="done" :tone="done.tone">{{ done.text }}
        <OpButton small :variant="BUTTON.ghost" @click="navigate(TAB.control)">Aller aux Volets</OpButton></OpStatline>
    </div>
  </OpCard>
</template>

<style scoped>
.flow{display:grid;gap:16px}
.step{display:grid;gap:12px;padding-left:14px;border-left:2px solid var(--accent-line)}
.step.done{border-left-color:color-mix(in srgb,var(--ok) 45%,var(--border))}
.gestures{margin:0;padding-left:1.3em;display:grid;gap:6px;font-size:.9rem}
.test-pad{max-width:360px}
.line{flex-wrap:nowrap}
.line .op-input{flex:1;min-width:0}
.link{background:none;border:0;padding:0;color:var(--accent);font:inherit;font-weight:600;cursor:pointer;text-decoration:underline}
</style>
