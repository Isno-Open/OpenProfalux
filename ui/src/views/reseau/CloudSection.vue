<script setup>
// Reseau › Cloud : le pilotage a distance par le cloud ISNO (app.isno.fr). Appairage par QR code (le
// boitier demande un code au portail, on confirme sur le telephone) ou par un code obtenu sur le compte ;
// puis Activer / Desactiver et Oublier. Le MQTT local n'est jamais touche : le cloud s'ajoute.
import { computed, onMounted, onUnmounted, ref } from 'vue';
import qrcode from 'qrcode-generator';
import { toast } from '../../store.js';
import api from '../../api/index.js';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import OpDetails from '../../components/base/OpDetails.vue';
import OpIcon from '../../components/base/OpIcon.vue';

const RELIRE_MS = 4000;     // appaire mais pas encore connecte : on relit l'etat tant que l'ecran est ouvert
const SUIVI_MS = 3000;      // suivi de la confirmation sur le telephone
const ETATS_QR = { expired: 'Code expiré : relancez l\'appairage.', denied: 'Refusé : ce boîtier appartient déjà à un autre compte.', error: 'Échec de l\'appairage.' };

const s = ref(null);
const serveur = ref('');
const code = ref('');
const qr = ref(null);           // { svg, hote, code, etat }
const occupe = ref('');         // 'qr' | 'code' | 'bascule'
const confirmerOubli = ref(false);
let relire = null, suivi = null, ouvert = true;

const statut = computed(() => {
  const x = s.value;
  if (!x) return { ton: 'neutre', texte: '…' };
  if (!x.paired) return { ton: 'neutre', texte: 'Non appairé' };
  if (!x.enabled) return { ton: 'neutre', texte: 'Appairé, cloud désactivé' };
  if (x.connected) return { ton: 'ok', texte: 'Connecté au cloud ISNO' };
  return { ton: 'danger', texte: 'Appairé, hors ligne' };
});

async function charger() {
  clearTimeout(relire);
  const x = await api.cloudEtat().catch(() => null);
  if (!x || !ouvert) return;
  s.value = x;
  if (x.paired) { arreterSuivi(); qr.value = null; }
  if (x.paired && x.enabled && !x.connected) relire = setTimeout(() => { if (!document.hidden) charger(); }, RELIRE_MS);
}
onMounted(charger);
onUnmounted(() => { ouvert = false; clearTimeout(relire); arreterSuivi(); });

function arreterSuivi() { clearTimeout(suivi); suivi = null; }
async function suivre() {
  const g = await api.cloudSuivi().catch(() => null);
  if (!ouvert) return;
  if (g && g.state === 'pending') { suivi = setTimeout(suivre, SUIVI_MS); return; }
  arreterSuivi();
  if (g && g.state === 'ready') { qr.value = null; toast(`Appairé : ${g.device_id || ''}`); charger(); return; }
  if (qr.value) qr.value.etat = (g && ETATS_QR[g.state]) || ETATS_QR.error;
}

/* Appairage par QR code (« a la Netflix ») : le boitier demande un code au portail, on affiche le QR et
   le code court, puis on suit l'etat jusqu'a la confirmation sur le telephone. */
async function parQr() {
  occupe.value = 'qr';
  arreterSuivi();
  try {
    const r = await api.cloudDemander(serveur.value.trim());
    if (r && r.err === 'paired') { toast('Déjà appairé : « Oublier » d\'abord pour réappairer'); charger(); return; }
    if (!r || !r.uri) { toast(`Portail injoignable (${(r && r.status) || '?'})`); return; }
    const q = qrcode(0, 'M');
    q.addData(r.uri);
    q.make();
    let hote = r.uri;
    try { const u = new URL(r.uri); hote = u.host + u.pathname; } catch { /* adresse brute */ }
    qr.value = { svg: q.createSvgTag({ cellSize: 5, margin: 4, scalable: true }), hote, code: r.user_code, etat: 'En attente de votre confirmation…' };
    suivi = setTimeout(suivre, SUIVI_MS);
  } catch { toast('Échec de la demande'); }
  finally { occupe.value = ''; }
}
async function parCode() {
  if (!code.value.trim()) { toast('Entre le code d\'appairage'); return; }
  occupe.value = 'code';
  try {
    const r = await api.cloudAppairer(code.value.trim(), serveur.value.trim());
    if (r && r.ok) { toast(`Appairé : ${r.device_id || ''}`); code.value = ''; }
    else toast(`Échec appairage (${(r && r.err) || '?'})`);
  } catch { toast('Échec appairage'); }
  finally { occupe.value = ''; charger(); }
}
async function basculer() {
  occupe.value = 'bascule';
  await api.cloudBasculer(!s.value.enabled).catch(() => {});
  occupe.value = '';
  charger();
}
async function oublier() {
  confirmerOubli.value = false;
  await api.cloudOublier().then(() => toast('Cloud oublié'), () => {});
  charger();
}
</script>

<template>
  <OpCard title="Cloud" sub="Pilotage à distance via le cloud ISNO.">
    <div class="cloud">
      <div class="etat">
        <OpIcon name="cloud" size="2.6em" />
        <div class="texte">
          <OpStatline :tone="statut.ton"><b>{{ statut.texte }}</b></OpStatline>
          <!-- Le nom vient du portail : affiche comme du texte, jamais comme du HTML. -->
          <div v-if="s && s.paired" class="ident">
            <b v-if="s.name">{{ s.name }}</b>
            <small v-if="s.device_id">identifiant cloud : {{ s.device_id }}</small>
          </div>
        </div>
      </div>

      <template v-if="s && !s.paired">
        <OpField label="Serveur" pour="cloud-serveur">
          <input id="cloud-serveur" v-model="serveur" class="op-input" placeholder="https://app.isno.fr" autocomplete="off" spellcheck="false">
        </OpField>
        <div class="rangee fin"><OpButton variant="primary" icon="cloud" :disabled="!!occupe" @click="parQr">{{ occupe === 'qr' ? 'Demande au portail…' : 'Appairer par QR code' }}</OpButton></div>
        <div v-if="qr" class="qr">
          <!-- QR engendre ici par qrcode-generator (MIT), a partir de l'adresse rendue par le portail. -->
          <div class="code-qr" aria-label="QR code d'appairage" v-html="qr.svg" />
          <div class="consignes">
            <p>Scannez ce QR code avec votre téléphone et confirmez depuis votre compte. Ou ouvrez <b>{{ qr.hote }}</b> et saisissez le code <b class="mono">{{ qr.code }}</b>.</p>
            <p class="hint" role="status">{{ qr.etat }}</p>
          </div>
        </div>
        <OpDetails titre="Ou avec un code obtenu sur votre compte">
          <OpField label="Code d'appairage" pour="cloud-code">
            <input id="cloud-code" v-model="code" class="op-input mono" placeholder="depuis votre compte sur app.isno.fr" autocomplete="off" spellcheck="false">
          </OpField>
          <div class="rangee fin"><OpButton variant="ghost" :disabled="!!occupe" @click="parCode">{{ occupe === 'code' ? 'Appairage…' : 'Appairer' }}</OpButton></div>
        </OpDetails>
      </template>

      <template v-if="s && s.paired">
        <div class="rangee fin">
          <OpButton variant="ghost" :disabled="!!occupe" @click="basculer">{{ s.enabled ? 'Désactiver' : 'Activer' }}</OpButton>
          <OpButton variant="ghost" @click="confirmerOubli = true">Oublier</OpButton>
        </div>
        <OpStatline v-if="confirmerOubli" tone="alerte">Oublier le cloud sur ce boîtier ? Le compte garde l'appareil, un nouvel appairage reste possible.
          <OpButton small variant="danger" @click="oublier">Oublier</OpButton><OpButton small @click="confirmerOubli = false">Annuler</OpButton></OpStatline>
      </template>
    </div>
  </OpCard>
</template>

<style scoped>
.cloud{display:grid;gap:14px}
.etat{display:flex;align-items:center;gap:14px}
.etat .texte{flex:1;display:grid;gap:6px;min-width:0}
.ident{display:flex;flex-direction:column;gap:2px;padding-left:2px}
.ident small{color:var(--muted);font-family:var(--mono);font-size:.74rem}
.qr{display:flex;flex-wrap:wrap;gap:14px;align-items:center}
/* Modules noirs sur fond blanc dans les deux themes : un QR en negatif ne se lit pas partout. */
.code-qr :deep(svg){display:block;width:220px;max-width:100%;height:auto;background:#fff;border-radius:8px}
.consignes{flex:1 1 220px;display:grid;gap:6px}
.consignes p{margin:0}
.mono{font-family:var(--mono)}
</style>
