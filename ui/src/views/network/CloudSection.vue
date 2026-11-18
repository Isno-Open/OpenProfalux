<script setup lang="ts">
// Reseau › Cloud : le pilotage a distance par le cloud ISNO (app.isno.fr). Appairage par QR code (le boitier
// demande un code au portail, on confirme sur le telephone) ou par un code obtenu sur le compte ; puis Activer /
// Desactiver et Oublier. Le MQTT local n'est jamais touche : le cloud s'ajoute.
import { computed, onMounted, onUnmounted, ref } from 'vue';
import qrcode from 'qrcode-generator';
import { toast } from '../../store';
import api from '../../api/index';
import type { CloudStatus } from '../../api/types';
import { BUTTON, CLOUD_ALREADY_PAIRED, GRANT_STATE, TONE, type Tone } from '../../domain';
import { ICON } from '../../icons';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import OpDetails from '../../components/base/OpDetails.vue';
import OpIcon from '../../components/base/OpIcon.vue';

const RELOAD_MS = 4000;     // appaire mais pas encore connecte : on relit l'etat tant que l'ecran est ouvert
const POLL_MS = 3000;       // suivi de la confirmation sur le telephone
const QR_STATES: Record<string, string> = {
  [GRANT_STATE.expired]: 'Code expiré : relancez l\'appairage.',
  [GRANT_STATE.denied]: 'Refusé : ce boîtier appartient déjà à un autre compte.',
  [GRANT_STATE.error]: 'Échec de l\'appairage.',
};
/* L'operation en cours, qui grise les boutons. */
const BUSY = { none: '', qr: 'qr', code: 'code', toggle: 'toggle' } as const;
type Busy = (typeof BUSY)[keyof typeof BUSY];

const cloud = ref<CloudStatus | null>(null);
const server = ref('');
const code = ref('');
const qr = ref<{ svg: string; host: string; code: string; state: string } | null>(null);
const busy = ref<Busy>(BUSY.none);
const confirmForget = ref(false);
let reloadTimer: ReturnType<typeof setTimeout> | undefined, pollTimer: ReturnType<typeof setTimeout> | undefined, mounted = true;

const statusLine = computed<{ tone: Tone; text: string }>(() => {
  const c = cloud.value;
  if (!c) return { tone: TONE.neutral, text: '…' };
  if (!c.paired) return { tone: TONE.neutral, text: 'Non appairé' };
  if (!c.enabled) return { tone: TONE.neutral, text: 'Appairé, cloud désactivé' };
  if (c.connected) return { tone: TONE.ok, text: 'Connecté au cloud ISNO' };
  return { tone: TONE.danger, text: 'Appairé, hors ligne' };
});

async function load(): Promise<void> {
  clearTimeout(reloadTimer);
  const c = await api.cloudStatus().catch(() => null);
  if (!c || !mounted) return;
  cloud.value = c;
  if (c.paired) { stopPolling(); qr.value = null; }
  if (c.paired && c.enabled && !c.connected) reloadTimer = setTimeout(() => { if (!document.hidden) load(); }, RELOAD_MS);
}
onMounted(load);
onUnmounted(() => { mounted = false; clearTimeout(reloadTimer); stopPolling(); });

function stopPolling(): void { clearTimeout(pollTimer); pollTimer = undefined; }
async function pollGrant(): Promise<void> {
  const g = await api.cloudPollGrant().catch(() => null);
  if (!mounted) return;
  if (g?.state === GRANT_STATE.pending) { pollTimer = setTimeout(pollGrant, POLL_MS); return; }
  stopPolling();
  if (g?.state === GRANT_STATE.ready) { qr.value = null; toast(`Appairé : ${g.device_id || ''}`); load(); return; }
  if (qr.value) qr.value.state = (g && QR_STATES[g.state]) || QR_STATES[GRANT_STATE.error] || '';
}

/* Appairage par QR code (« a la Netflix ») : le boitier demande un code au portail, on affiche le QR et le code
   court, puis on suit l'etat jusqu'a la confirmation sur le telephone. */
async function pairByQr(): Promise<void> {
  busy.value = BUSY.qr;
  stopPolling();
  try {
    const r = await api.cloudRequestGrant(server.value.trim());
    if (r?.err === CLOUD_ALREADY_PAIRED) { toast('Déjà appairé : « Oublier » d\'abord pour réappairer'); load(); return; }
    if (!r?.uri) { toast(`Portail injoignable (${r?.status || '?'})`); return; }
    const q = qrcode(0, 'M');
    q.addData(r.uri);
    q.make();
    let host = r.uri;
    try { const u = new URL(r.uri); host = u.host + u.pathname; } catch { /* adresse brute */ }
    qr.value = { svg: q.createSvgTag({ cellSize: 5, margin: 4, scalable: true }), host, code: r.user_code ?? '', state: 'En attente de votre confirmation…' };
    pollTimer = setTimeout(pollGrant, POLL_MS);
  } catch { toast('Échec de la demande'); }
  finally { busy.value = BUSY.none; }
}
async function pairByCode(): Promise<void> {
  if (!code.value.trim()) { toast('Entre le code d\'appairage'); return; }
  busy.value = BUSY.code;
  try {
    const r = await api.cloudPair(code.value.trim(), server.value.trim());
    if (r?.ok) { toast(`Appairé : ${r.device_id || ''}`); code.value = ''; }
    else toast(`Échec appairage (${r?.err || '?'})`);
  } catch { toast('Échec appairage'); }
  finally { busy.value = BUSY.none; load(); }
}
async function toggle(): Promise<void> {
  busy.value = BUSY.toggle;
  await api.cloudToggle(!cloud.value?.enabled).catch(() => {});
  busy.value = BUSY.none;
  load();
}
async function forget(): Promise<void> {
  confirmForget.value = false;
  await api.cloudForget().then(() => toast('Cloud oublié'), () => {});
  load();
}
</script>

<template>
  <OpCard title="Cloud" sub="Pilotage à distance via le cloud ISNO.">
    <div class="cloud">
      <div class="state">
        <OpIcon :name="ICON.cloud" size="2.6em" />
        <div class="text">
          <OpStatline :tone="statusLine.tone"><b>{{ statusLine.text }}</b></OpStatline>
          <!-- Le nom vient du portail : affiche comme du texte, jamais comme du HTML. -->
          <div v-if="cloud?.paired" class="ident">
            <b v-if="cloud.name">{{ cloud.name }}</b>
            <small v-if="cloud.device_id">identifiant cloud : {{ cloud.device_id }}</small>
          </div>
        </div>
      </div>

      <template v-if="cloud && !cloud.paired">
        <OpField label="Serveur" control="cloud-server">
          <input id="cloud-server" v-model="server" class="op-input" placeholder="https://app.isno.fr" autocomplete="off" spellcheck="false">
        </OpField>
        <div class="row end"><OpButton :variant="BUTTON.primary" :icon="ICON.cloud" :disabled="!!busy" @click="pairByQr">{{ busy === BUSY.qr ? 'Demande au portail…' : 'Appairer par QR code' }}</OpButton></div>
        <div v-if="qr" class="qr">
          <!-- QR engendre ici par qrcode-generator (MIT), a partir de l'adresse rendue par le portail. -->
          <div class="qr-code" aria-label="QR code d'appairage" v-html="qr.svg" />
          <div class="instructions">
            <p>Scannez ce QR code avec votre téléphone et confirmez depuis votre compte. Ou ouvrez <b>{{ qr.host }}</b> et saisissez le code <b class="mono">{{ qr.code }}</b>.</p>
            <p class="hint" role="status">{{ qr.state }}</p>
          </div>
        </div>
        <OpDetails title="Ou avec un code obtenu sur votre compte">
          <OpField label="Code d'appairage" control="cloud-code">
            <input id="cloud-code" v-model="code" class="op-input mono" placeholder="depuis votre compte sur app.isno.fr" autocomplete="off" spellcheck="false">
          </OpField>
          <div class="row end"><OpButton :variant="BUTTON.ghost" :disabled="!!busy" @click="pairByCode">{{ busy === BUSY.code ? 'Appairage…' : 'Appairer' }}</OpButton></div>
        </OpDetails>
      </template>

      <template v-if="cloud?.paired">
        <div class="row end">
          <OpButton :variant="BUTTON.ghost" :disabled="!!busy" @click="toggle">{{ cloud.enabled ? 'Désactiver' : 'Activer' }}</OpButton>
          <OpButton :variant="BUTTON.ghost" @click="confirmForget = true">Oublier</OpButton>
        </div>
        <OpStatline v-if="confirmForget" :tone="TONE.warning">Oublier le cloud sur ce boîtier ? Le compte garde l'appareil, un nouvel appairage reste possible.
          <OpButton small :variant="BUTTON.danger" @click="forget">Oublier</OpButton><OpButton small @click="confirmForget = false">Annuler</OpButton></OpStatline>
      </template>
    </div>
  </OpCard>
</template>

<style scoped>
.cloud{display:grid;gap:14px}
.state{display:flex;align-items:center;gap:14px}
.state .text{flex:1;display:grid;gap:6px;min-width:0}
.ident{display:flex;flex-direction:column;gap:2px;padding-left:2px}
.ident small{color:var(--muted);font-family:var(--mono);font-size:.74rem}
.qr{display:flex;flex-wrap:wrap;gap:14px;align-items:center}
/* Modules noirs sur fond blanc dans les deux themes : un QR en negatif ne se lit pas partout. */
.qr-code :deep(svg){display:block;width:220px;max-width:100%;height:auto;background:#fff;border-radius:8px}
.instructions{flex:1 1 220px;display:grid;gap:6px}
.instructions p{margin:0}
.mono{font-family:var(--mono)}
</style>
