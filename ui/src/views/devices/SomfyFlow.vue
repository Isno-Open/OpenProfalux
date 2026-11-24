<script lang="ts">
// Creer un volet Somfy RTS (433,42 MHz) : pas besoin de telecommande d'origine. On choisit une adresse (24 bits),
// le boitier la fait apprendre au moteur en mode appairage, puis on teste.
import { ref } from 'vue';
import type { Tone } from '../../domain';

/* Le volet en cours vit hors de l'ecran, qui est demonte a chaque changement d'onglet. Il est deja cree dans le
   boitier, et l'appairage ne s'emet que d'ici : revenir avant d'appairer doit le retrouver, comme dans l'ancienne
   interface, ou le panneau restait dans la page. « Creer un autre volet » repart d'un formulaire vide. */
const name = ref('');
const address = ref('');
const created = ref('');          // id du volet cree
const message = ref<{ tone: Tone; text: string } | null>(null);
const paired = ref(false);
function startOver(): void {
  name.value = ''; address.value = ''; created.value = ''; message.value = null; paired.value = false;
}
</script>

<script setup lang="ts">
import { computed } from 'vue';
import { navigate } from '../../router';
import { state, sendCommand, refresh, typeOf } from '../../store';
import { commandLabels, hasComma } from '../../profiles';
import api from '../../api/index';
import { BUTTON, COMMAND, DEVICE_TYPE, DEVICE_TYPE_CODE, SECTION, TAB, TONE, type Command } from '../../domain';
import { ICON } from '../../icons';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import OpSteps from '../../components/base/OpSteps.vue';
import ShutterControl from '../../components/domain/ShutterControl.vue';

const NAME_EXISTS = 'exists';   // refus du firmware : un appareil porte deja ce nom
const ADDRESS_PATTERN = /^0x[0-9a-f]{6}$/i;
const ADDRESS_MAX = 0xFFFFFF;
/* Le volet en cours a ete supprime entre-temps (Appareils) : plus rien a appairer. */
if (created.value && state.status && !state.status.volets.some(d => d.id === created.value)) startOver();
const step = computed(() => (!created.value ? 0 : !paired.value ? 1 : 2));
const dpad = computed(() => commandLabels(typeOf({ type: DEVICE_TYPE_CODE.shutter })));
const sending = computed<Command | ''>(() => (state.sending && state.sending.id === created.value ? state.sending.cmd : ''));

function generate(): void {
  address.value = `0x${Math.floor(Math.random() * ADDRESS_MAX).toString(16).toUpperCase().padStart(6, '0')}`;
}
async function create(): Promise<void> {
  const n = name.value.trim();
  if (!n || hasComma(n)) { message.value = { tone: TONE.danger, text: 'Donne un nom au volet, sans virgule.' }; return; }
  if (!ADDRESS_PATTERN.test(address.value.trim())) { message.value = { tone: TONE.danger, text: 'L\'adresse fait 6 chiffres hexadécimaux, par exemple 0x1A2B3C. Clique Générer.' }; return; }
  try {
    await api.somfyCreate({ id: n, address: address.value.trim() });
    created.value = n;
    message.value = { tone: TONE.ok, text: `Volet « ${n} » créé. Appaire maintenant le moteur.` };
    refresh();
  } catch (e) {
    message.value = { tone: TONE.danger, text: e === NAME_EXISTS ? 'Ce nom existe déjà.' : 'Échec de la création.' };
  }
}
async function pair(): Promise<void> {
  await sendCommand({ id: created.value, cmd: COMMAND.prog });
  paired.value = true;
}
</script>

<template>
  <OpCard title="Créer un volet Somfy" sub="Moteur Somfy RTS, 433,42 MHz. Pas besoin de télécommande d'origine : on choisit une adresse et le moteur l'apprend.">
    <template #before><button type="button" class="back-link" @click="navigate(`${TAB.remotes}/${SECTION.add}/${DEVICE_TYPE.shutter}`)">‹ Ajouter un appareil</button></template>
    <div class="flow">
      <OpSteps :steps="['Créer', 'Appairer', 'Tester']" :current="step" />
      <OpField label="Nom du volet" control="somfy-name">
        <input id="somfy-name" v-model="name" class="op-input" placeholder="ex : Volet salon Somfy" :maxlength="state.profiles?.name_max || 23" :disabled="!!created">
      </OpField>
      <OpField label="Adresse (24 bits)" control="somfy-address" hint="L'identité de la télécommande virtuelle. Garde-la : c'est elle que le moteur retient.">
        <div class="row line">
          <input id="somfy-address" v-model="address" class="op-input mono" placeholder="0x123456" spellcheck="false" autocomplete="off" :disabled="!!created">
          <OpButton :disabled="!!created" @click="generate">Générer</OpButton>
        </div>
      </OpField>
      <div class="row">
        <OpButton :variant="BUTTON.primary" :disabled="!!created" @click="create">Créer le volet</OpButton>
        <OpButton v-if="created" :variant="BUTTON.ghost" @click="startOver">Créer un autre volet</OpButton>
      </div>
      <OpStatline v-if="message" :tone="message.tone">{{ message.text }}</OpStatline>

      <section v-if="created" class="step">
        <h3 class="section-title">Appairer le moteur</h3>
        <p class="hint">Mets le moteur en attente d'appairage : appui long sur le bouton PROG de sa télécommande d'origine, le moteur fait
          un va-et-vient. Puis clique : le boîtier émet la commande d'appairage. Un nouveau va-et-vient : c'est appairé.</p>
        <div class="row"><OpButton :variant="paired ? BUTTON.normal : BUTTON.primary" :icon="ICON.remote" :disabled="state.radioBusy" @click="pair">Émettre l'appairage</OpButton></div>
        <OpStatline v-if="paired" :tone="TONE.ok">Appairage émis. Le moteur a fait un va-et-vient ? Teste ci-dessous.</OpStatline>
      </section>
      <section v-if="paired" class="step">
        <h3 class="section-title">Tester</h3>
        <ShutterControl class="test-pad" :labels="dpad" :sending="sending" :disabled="state.radioBusy" @command="cmd => sendCommand({ id: created, cmd })" />
        <p class="hint">Le volet est aussi dans l'onglet <b>Volets</b> et pilotable depuis Home Assistant.</p>
      </section>
    </div>
  </OpCard>
</template>

<style scoped>
.flow{display:grid;gap:14px}
.step{display:grid;gap:12px;padding-left:14px;border-left:2px solid var(--accent-line)}
.line{flex-wrap:nowrap}
.line .op-input{flex:1;min-width:0}
.test-pad{max-width:360px}
</style>
