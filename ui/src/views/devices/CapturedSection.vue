<script setup lang="ts">
// Les telecommandes captees par le boitier (meme celle d'un voisin), et celles qu'il ignore. Une telecommande
// supprimee revient si elle est de nouveau captee ; une telecommande ignoree ne revient plus.
import { computed, reactive, ref } from 'vue';
import { state, refresh, toast, renameFailureText } from '../../store';
import { hasComma, isSomfy } from '../../profiles';
import api from '../../api/index';
import { BUTTON, COMMAND, TONE, type MoveCommand } from '../../domain';
import OpCard from '../../components/base/OpCard.vue';
import OpBadge from '../../components/base/OpBadge.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpSelect from '../../components/base/OpSelect.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import OpTagBox from '../../components/base/OpTagBox.vue';
import SignalBars from '../../components/domain/SignalBars.vue';
import BatteryBadge from '../../components/domain/BatteryBadge.vue';

const DEFAULT_MAX = 16;
const status = computed(() => state.status);
const remotes = computed(() => Object.entries(status.value?.remotes ?? {}));
const max = computed(() => status.value?.remotes_max ?? DEFAULT_MAX);
const full = computed(() => remotes.value.length >= max.value);
const ignored = computed(() => status.value?.remotes_ignored ?? []);
const rssiOf = (serial: string): number | null => status.value?.rf.find(x => x.serial === serial)?.rssi ?? null;
const usedBy = (serial: string): string => status.value?.volets.find(d => d.serials.includes(serial))?.id ?? '';
const describe = (serial: string, name: string): string => (name ? `« ${name} » (${serial})` : serial);

const creating = ref('');    // serial pour lequel on saisit le nom du volet
const deviceName = ref('');
function openCreation(serial: string, name: string): void { attaching.value = ''; creating.value = serial; deviceName.value = name; }
/* Associer la telecommande a un volet enrole (telecommande virtuelle Profalux) deja cree, pour que le boitier suive
   sa position quand on le manoeuvre avec la vraie telecommande (issue #159). Un volet clone n'est pas propose : ses
   commandes emises seraient remplacees. */
const enrolled = computed(() => (status.value?.volets ?? []).filter(d => d.virt && !isSomfy(d)).map(d => ({ value: d.id, label: d.id })));
const attaching = ref('');   // serial pour lequel on choisit le volet
const target = ref<string | number>('');
function openAttachment(serial: string): void { creating.value = ''; attaching.value = serial; target.value = enrolled.value[0]?.value ?? ''; }
const symbols = (list: MoveCommand[]): string => list.map(a => SYMBOL[a]).join(' ');
/* Commandes trouvees dans le journal RF et commandes manquantes, dans l'ordre du pave. */
function movesOf(reply: { learned?: string }): { learned: MoveCommand[]; missing: MoveCommand[] } {
  const found = (reply.learned || '').split(',').filter(Boolean);
  return { learned: MOVES.filter(m => found.includes(m)), missing: MOVES.filter(m => !found.includes(m)) };
}
/* Volet enrole qui suit deja cette telecommande, mais pas pour toutes ses commandes : le journal RF n'en portait
   qu'une partie a l'association. Le bouton reste alors offert, pour ranger les commandes captees depuis. */
function incomplete(serial: string): string {
  const d = status.value?.volets.find(x => x.virt && !isSomfy(x) && x.serials.includes(serial));
  return d && MOVES.some(m => !d.cmd[m]) ? d.id : '';
}
async function attach(serial: string, id: string): Promise<void> {
  if (!id) return;
  try {
    const { learned, missing } = movesOf(await api.attachRemote(serial, id));
    toast(`Télécommande associée à « ${id} » : ${symbols(learned)} suivis${missing.length ? ` · ${symbols(missing)} absents du journal, appuie dessus puis recommence` : ''}`);
    attaching.value = '';
    refresh();
  } catch (e) { toast(`Refusé : ${e}`); }
}
const SYMBOL: Record<MoveCommand, string> = { [COMMAND.up]: '▲', [COMMAND.stop]: '■', [COMMAND.down]: '▼' };
const MOVES: MoveCommand[] = [COMMAND.up, COMMAND.stop, COMMAND.down];
async function createDevice(serial: string): Promise<void> {
  const n = deviceName.value.trim();
  if (!n || hasComma(n)) { toast('Donne un nom au volet, sans virgule'); return; }
  try {
    const d = await api.deviceFromRemote(serial, n);
    const { learned, missing } = movesOf(d);
    toast(`Volet « ${n} » créé : ${symbols(learned)} appris${missing.length ? ` · ${symbols(missing)} à apprendre` : ''}`);
    creating.value = '';
    refresh();
  } catch (e) { toast(`Refusé : ${e}`); }
}
/* Le nom en cours de saisie, par telecommande : le statut relu toutes les 3 s ne l'ecrase pas. Il redevient celui du
   boitier une fois enregistre. */
const drafts = reactive<Record<string, string>>({});
async function rename(serial: string, name: string): Promise<void> {
  await api.remoteRename(serial, name.trim()).catch(e => toast(renameFailureText(e)));
  await refresh();
  delete drafts[serial];
}
function onNameInput(serial: string, e: Event): void { if (e.target instanceof HTMLInputElement) drafts[serial] = e.target.value; }
function onNameChange(serial: string, e: Event): void { if (e.target instanceof HTMLInputElement) rename(serial, e.target.value); }
/* Sans confirmation : une telecommande supprimee revient d'un appui, une ignoree d'un clic sur sa croix. */
async function remove(serial: string, name: string): Promise<void> {
  try { await api.remoteDelete(serial); toast(`${describe(serial, name)} supprimée`); } catch (e) { toast(`Refusé : ${e}`); }
  refresh();
}
async function ignore(serial: string, name: string): Promise<void> {
  try { await api.remoteIgnore(serial); toast(`${describe(serial, name)} ignorée`); } catch (e) { toast(`Refusé : ${e}`); }
  refresh();
}
async function addIgnored(serial: string): Promise<void> {
  try { await api.remoteIgnore(serial.trim()); toast('Télécommande ignorée'); } catch (e) { toast(`Refusé : ${e}`); }
  refresh();
}
async function unignore(serial: string): Promise<void> {
  try { await api.remoteUnignore(serial); toast(`${serial} n'est plus ignorée`); } catch { toast(`Refusé : ${serial} reste ignorée`); }
  refresh();
}
</script>

<template>
  <OpCard title="Télécommandes captées" sub="Toute télécommande captée par le boîtier, même celle d'un voisin, s'enregistre ici. Supprime celles qui ne servent pas : une télécommande supprimée revient si elle est de nouveau captée.">
    <template #actions><OpBadge :tone="full ? TONE.danger : TONE.neutral">{{ remotes.length }} / {{ max }}</OpBadge></template>
    <div class="captured">
      <OpStatline v-if="full" :tone="TONE.danger"><b>Liste pleine :</b> une nouvelle télécommande ne pourra pas être enregistrée. Supprime une télécommande inutile.</OpStatline>
      <div v-for="[serial, name] in remotes" :key="serial" class="remote">
        <code class="serial">{{ serial }}</code>
        <input class="op-input name" :value="drafts[serial] ?? name" placeholder="nom…" :aria-label="`Nom de la télécommande ${serial}`"
               @input="e => onNameInput(serial, e)" @change="e => onNameChange(serial, e)">
        <span class="state">
          <SignalBars v-if="rssiOf(serial) != null" :rssi="rssiOf(serial) ?? 0" />
          <BatteryBadge :battery="status?.remote_battery?.[serial] ?? null" />
        </span>
        <span class="usage">
          <span v-if="usedBy(serial)" class="row creation">
            <OpBadge :tone="TONE.accent">{{ usedBy(serial) }}</OpBadge>
            <OpButton v-if="incomplete(serial)" small title="Appuie sur les boutons manquants de la télécommande, puis clique : le boîtier range leurs trames pour suivre la position"
                      @click="attach(serial, incomplete(serial))">Compléter le suivi</OpButton>
          </span>
          <span v-else-if="creating === serial" class="row creation">
            <input v-model="deviceName" class="op-input" placeholder="nom du volet" aria-label="Nom du volet à créer"
                   @keydown.enter="createDevice(serial)" @keydown.esc="creating = ''">
            <OpButton small :variant="BUTTON.primary" @click="createDevice(serial)">Créer</OpButton>
          </span>
          <span v-else-if="attaching === serial" class="row creation">
            <OpSelect v-model="target" :options="enrolled" aria-label="Volet enrôlé à associer" />
            <OpButton small :variant="BUTTON.primary" @click="attach(serial, String(target))">Associer</OpButton>
            <OpButton small :variant="BUTTON.ghost" @click="attaching = ''">Annuler</OpButton>
          </span>
          <span v-else class="row creation">
            <OpButton small title="Créer un volet avec les dernières trames de cette télécommande dans le journal RF" @click="openCreation(serial, name)">+ Créer le volet</OpButton>
            <OpButton v-if="enrolled.length" small title="Suivre la position d'un volet enrôlé quand il est manœuvré avec cette télécommande" @click="openAttachment(serial)">Associer à un volet</OpButton>
          </span>
        </span>
        <span class="actions">
          <OpButton small :variant="BUTTON.ghost" title="Ne plus l'enregistrer quand elle est captée" @click="ignore(serial, name)">Ignorer</OpButton>
          <OpButton small :variant="BUTTON.danger" @click="remove(serial, name)">Supprimer</OpButton>
        </span>
      </div>
      <p v-if="!remotes.length" class="hint">Aucune télécommande enregistrée.</p>

      <h3 class="section-title">Télécommandes ignorées</h3>
      <p class="hint">Le boîtier ne les enregistre plus quand il les capte (celle d'un voisin, par exemple). Retire-en une avec sa croix, ou ajoute un serial à la main.</p>
      <OpTagBox :tags="ignored" mono placeholder="0x… puis Entrée" @add="addIgnored" @remove="unignore" />
    </div>
  </OpCard>
</template>

<style scoped>
.captured{display:grid;gap:9px}
.remote{display:grid;grid-template-columns:auto minmax(120px,1fr) auto minmax(150px,auto) auto;align-items:center;gap:10px;padding:9px 12px;border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surface-2)}
.serial{font-family:var(--mono);font-size:.82rem;padding:2px 7px;border-radius:6px;background:var(--surface);border:1px solid var(--border)}
.name{padding-top:6px;padding-bottom:6px}
.state,.actions{display:inline-flex;align-items:center;gap:6px}
.usage{display:flex;align-items:center;min-width:0}
.creation{flex-wrap:nowrap}
.creation .op-input{min-width:0;width:140px;padding-top:5px;padding-bottom:5px}
@media (max-width:760px){
  .remote{grid-template-columns:auto 1fr auto}
  .usage{grid-column:1 / 3}
  .actions{grid-column:3;justify-content:flex-end}
}
/* Telephone : serial et signal, puis le nom, puis l'usage et les actions sur une meme ligne. */
@media (max-width:420px){
  .remote{grid-template-columns:1fr auto;gap:8px}
  .serial{grid-area:1 / 1;justify-self:start}
  .state{grid-area:1 / 2}
  .name{grid-area:2 / 1 / 3 / 3}
  .usage{grid-area:3 / 1}
  .actions{grid-area:3 / 2}
  .creation .op-input{width:110px}
}
</style>
