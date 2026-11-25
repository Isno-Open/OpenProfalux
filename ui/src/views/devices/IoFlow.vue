<script setup lang="ts">
// Ajouter un volet io-homecontrol 1W (Velux, #87, #118) : le boitier devient une NOUVELLE telecommande du volet,
// que le moteur apprend. Ce n'est pas un clone : on ne capte pas les touches, les commandes io sont celles du
// protocole. On capte UN appui de la telecommande d'origine, qui donne au boitier la destination et la charge utile
// de ce volet ; puis la telecommande d'origine met le moteur en apprentissage, et le boitier envoie sa cle.
import { computed, onMounted, ref } from 'vue';
import { navigate, route } from '../../router';
import { state, sendCommand, refresh, typeOf, withRadio } from '../../store';
import { commandLabels, flowOf, hasComma, profileById } from '../../profiles';
import api from '../../api/index';
import type { Profile } from '../../api/types';
import { BUTTON, DEVICE_TYPE, DEVICE_TYPE_CODE, FLAG, FLOW, SECTION, TAB, TONE, type Command, type Tone } from '../../domain';
import { ICON } from '../../icons';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import OpSteps from '../../components/base/OpSteps.vue';
import OpProgress from '../../components/base/OpProgress.vue';
import ShutterControl from '../../components/domain/ShutterControl.vue';

const NAME_EXISTS = 'exists';   // refus du mode demo : un appareil porte deja ce nom
/* Correction de quartz io admise, en kHz : miroir de IO_FREQ_OFFSET_MAX_KHZ (firmware/main/io/io_radio.h), le
   firmware refuse au-dela. */
const IO_FREQ_OFFSET_MAX_KHZ = 100;

/* L'adresse est lue une fois : l'ecran est reconstruit a chaque adresse (DevicesView). */
const profileId = route.rest[0] ? decodeURIComponent(route.rest[0]) : '';
const profile = computed<Profile | null>(() => {
  const p = profileById(profileId);
  return p && flowOf(p) === FLOW.enrolIo ? p : null;
});

const name = ref('');
const frame = ref('');            // commande 0x00 captee, en hexa
const sniffing = ref(false);
const created = ref('');          // id du volet cree (appaire)
const message = ref<{ tone: Tone; text: string } | null>(null);
const offset = ref<string | number>(0);
const offsetSaved = ref<{ tone: Tone; text: string } | null>(null);
const step = computed(() => (!frame.value ? 0 : !created.value ? 1 : 2));
const dpad = computed(() => commandLabels(typeOf({ type: DEVICE_TYPE_CODE.shutter })));
const sending = computed<Command | ''>(() => (state.sending && state.sending.id === created.value ? state.sending.cmd : ''));

onMounted(() => {
  api.config().then(c => { offset.value = c.io_freq_offset_khz ?? 0; }, () => {});
});

async function sniff(): Promise<void> {
  message.value = null;
  sniffing.value = true;
  try {
    const r = await withRadio(() => api.ioSniff());
    if (r.ok !== FLAG.on || !r.frame) { message.value = { tone: TONE.danger, text: 'Rien capté en 8 s. Rapproche la télécommande du boîtier et réessaie.' }; return; }
    if (r.usable !== FLAG.on) { message.value = { tone: TONE.warning, text: 'Une trame io a été captée, mais ce n\'est pas une commande complète. Réessaie avec Monter, Descendre ou Stop.' }; return; }
    frame.value = r.frame;
    message.value = { tone: TONE.ok, text: 'Commande captée. Passe à l\'appairage.' };
  } catch {
    message.value = { tone: TONE.danger, text: 'Capture impossible : la radio est occupée, ou le boîtier n\'a pas de radio 868.' };
  } finally { sniffing.value = false; }
}

async function pair(): Promise<void> {
  const p = profile.value;
  const n = name.value.trim();
  if (!p) return;
  if (!n || hasComma(n)) { message.value = { tone: TONE.danger, text: 'Donne un nom au volet, sans virgule.' }; return; }
  try {
    const r = await withRadio(() => api.ioPair({ profile: p.id, frame: frame.value, name: n }));
    created.value = r.id ?? n;
    message.value = { tone: TONE.ok, text: `Clé envoyée, volet « ${created.value} » créé. Le moteur a fait un va-et-vient ? Teste ci-dessous.` };
    refresh();
  } catch (e) {
    message.value = { tone: TONE.danger, text: e === NAME_EXISTS ? 'Ce nom existe déjà.'
      : `Échec de l'appairage${typeof e === 'string' && e ? ` : ${e}` : ''}. Rien n'est réessayé automatiquement (batterie des moteurs solaires).` };
  }
}

async function saveOffset(): Promise<void> {
  const v = Number(offset.value);
  if (!Number.isInteger(v) || Math.abs(v) > IO_FREQ_OFFSET_MAX_KHZ) {
    offsetSaved.value = { tone: TONE.danger, text: `Un nombre entier entre -${IO_FREQ_OFFSET_MAX_KHZ} et ${IO_FREQ_OFFSET_MAX_KHZ}.` };
    return;
  }
  const r = await api.saveConfig({ io_freq_offset_khz: v }).catch(() => null);
  offsetSaved.value = r ? { tone: TONE.ok, text: 'Correction enregistrée.' } : { tone: TONE.danger, text: 'Refusé : correction non enregistrée.' };
}
</script>

<template>
  <OpCard :title="profile ? profile.name : 'Volet io-homecontrol'" sub="io-homecontrol 1W, 868,95 MHz. Le boîtier devient une nouvelle télécommande du volet ; la télécommande d'origine continue de marcher.">
    <template #before><button type="button" class="back-link" @click="navigate(`${TAB.remotes}/${SECTION.add}/${DEVICE_TYPE.shutter}`)">‹ Ajouter un appareil</button></template>
    <OpStatline v-if="!profile" :tone="TONE.danger">Profil io inconnu. Reviens à « Ajouter un appareil ».</OpStatline>
    <div v-else class="flow">
      <OpSteps :steps="['Capter', 'Appairer', 'Tester']" :current="step" />

      <section class="step">
        <h3 class="section-title">Capter un appui de la télécommande d'origine</h3>
        <p class="hint">Tiens la télécommande d'origine près du boîtier. Clique, puis appuie une fois sur <b>Monter</b>, <b>Descendre</b> ou
          <b>Stop</b> (8 s au plus). Le boîtier y lit la destination de ce volet ; il ne copie pas la télécommande.</p>
        <div class="row"><OpButton :variant="frame ? BUTTON.normal : BUTTON.primary" :icon="ICON.remote" :disabled="state.radioBusy || !!created" @click="sniff">
          {{ frame ? 'Capter à nouveau' : 'Capter' }}</OpButton></div>
        <OpProgress v-if="sniffing" label="Écoute io" />
        <p v-if="frame" class="hint mono">{{ frame }}</p>
      </section>

      <section v-if="frame" class="step">
        <h3 class="section-title">Appairer</h3>
        <OpField label="Nom du volet" control="io-name">
          <input id="io-name" v-model="name" class="op-input" placeholder="ex : Velux chambre" :maxlength="state.profiles?.name_max || 23" :disabled="!!created">
        </OpField>
        <p class="hint">Mets le moteur en apprentissage avec la télécommande d'origine : <b>appui long sur RESET</b> (une dizaine de secondes),
          jusqu'au va-et-vient du volet. Puis clique : le boîtier envoie sa clé une seule fois. Un nouveau va-et-vient : c'est appairé.</p>
        <div class="row"><OpButton :variant="BUTTON.primary" :disabled="state.radioBusy || !!created" @click="pair">Envoyer la clé</OpButton></div>
      </section>

      <OpStatline v-if="message" :tone="message.tone">{{ message.text }}</OpStatline>

      <section v-if="created" class="step">
        <h3 class="section-title">Tester</h3>
        <ShutterControl class="test-pad" :labels="dpad" :sending="sending" :disabled="state.radioBusy" @command="cmd => sendCommand({ id: created, cmd })" />
        <p class="hint">Le volet est aussi dans l'onglet <b>Volets</b> et pilotable depuis Home Assistant. Le moteur io 1W ne répond rien :
          la position est estimée, et les appuis sur la télécommande d'origine ne sont pas suivis.</p>
      </section>

      <details class="advanced">
        <summary>Rien ne passe ? Correction de fréquence io</summary>
        <p class="hint">Le quartz de certains modules est décalé (un M5Stack de -58 kHz). Sans correction, l'io ne passe pas. Mets l'écart
          mesuré sur cette carte, le même que celui trouvé par « Caler sur ma télécommande » en Profalux.</p>
        <OpField label="Correction (kHz)" control="io-offset">
          <div class="row line">
            <input id="io-offset" v-model="offset" class="op-input mono" type="number" step="1" :min="-IO_FREQ_OFFSET_MAX_KHZ" :max="IO_FREQ_OFFSET_MAX_KHZ">
            <OpButton @click="saveOffset">Enregistrer</OpButton>
          </div>
        </OpField>
        <OpStatline v-if="offsetSaved" :tone="offsetSaved.tone">{{ offsetSaved.text }}</OpStatline>
      </details>
    </div>
  </OpCard>
</template>

<style scoped>
.flow{display:grid;gap:14px}
.step{display:grid;gap:12px;padding-left:14px;border-left:2px solid var(--accent-line)}
.line{flex-wrap:nowrap}
.line .op-input{flex:1;min-width:0}
.mono{font-family:var(--mono);font-size:.78rem;overflow-wrap:anywhere}
.test-pad{max-width:360px}
.advanced{display:grid;gap:10px}
.advanced summary{cursor:pointer;color:var(--muted)}
</style>
