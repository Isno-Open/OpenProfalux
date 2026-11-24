<script setup lang="ts">
// Systeme › Radio : le diagnostic de chaque puce (detectee, emission, GDO0, SPI), un test de reception par radio,
// et la capture brute avec rejeu.
import { computed, onMounted, ref } from 'vue';
import { withRadio } from '../../store';
import api from '../../api/index';
import type { Diag, RadioDetail } from '../../api/types';
import { FAILURE, TONE, type Tone } from '../../domain';
import { ICON } from '../../icons';
import OpCard from '../../components/base/OpCard.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import OpProgress from '../../components/base/OpProgress.vue';
import RadioList from '../../components/domain/RadioList.vue';
import CapturePanel from '../../components/domain/CapturePanel.vue';

/* Valeurs de /api/diag : PARTNUM du CC1101, VERSION absente (0 ou 255), TX et GDO0 (1 = bon, 0 = en defaut). */
const CC1101_PARTNUM = 0, NO_VERSION = [0, 255], OK = 1, FAULT = 0;
interface Message { tone: Tone; text: string; running?: boolean }
const radios = ref<RadioDetail[]>([]);
const diag = ref<Diag | null>(null);
const testingTx = ref(false);
const rx = ref<Message | null>(null);
const diagFailed = ref(false);
/* Deux lectures independantes : l'echec de l'une ne laisse pas l'autre en « Chargement… ». */
onMounted(() => {
  api.radioDetails().then(r => { radios.value = r.radios ?? []; }, () => {});
  api.diag().then(d => { diag.value = d; }, () => { diagFailed.value = true; });
});
const hex = (n: number): string => `0x${Number(n).toString(16).toUpperCase().padStart(2, '0')}`;
const verdict = computed<Message>(() => {
  const x = diag.value;
  if (!x) return diagFailed.value ? { tone: TONE.danger, text: 'Diagnostic non lu : le boîtier n\'a pas répondu. Rouvre cette section pour réessayer.' } : { tone: TONE.neutral, text: 'Chargement…' };
  if (!(x.partnum === CC1101_PARTNUM && !NO_VERSION.includes(x.version))) return { tone: TONE.danger, text: `Module non détecté (câblage, SPI). VERSION ${hex(x.version)}` };
  if (x.spi_fails > 0) return { tone: TONE.danger, text: 'Réglages radio non pris par le module : coupe l\'alimentation puis redémarre.' };
  if (x.tx_ok !== OK) return { tone: TONE.danger, text: 'Détecté, mais l\'émission ne marche pas.' };
  if (x.gdo0 === FAULT) return { tone: TONE.danger, text: 'GDO0 tenu par un autre composant : l\'émission n\'atteint pas le module.' };
  return { tone: TONE.ok, text: 'Module OK : détecté, et l\'émission marche.' };
});
const summary = computed(() => {
  const x = diag.value;
  if (!x) return '';
  return `PARTNUM ${hex(x.partnum)} · VERSION ${hex(x.version)} · TX ${x.tx_ok === OK ? 'OK' : x.tx_ok === FAULT ? 'HS' : 'non testé'}`
    + (x.gdo0 === OK ? ' · GDO0 libre' : x.gdo0 === FAULT ? ` · GDO0 bloqué à ${x.gdo0_lvl}` : '')
    + (x.spi_retries || x.spi_fails ? ` · SPI : ${x.spi_retries} reprise(s), ${x.spi_fails} échec(s)` : '');
});
async function retestTx(): Promise<void> {
  testingTx.value = true;
  await withRadio(() => api.diagTx()).catch(() => {});
  diag.value = await api.diag().catch(() => diag.value);
  testingTx.value = false;
}
async function testRx(r: RadioDetail): Promise<void> {
  rx.value = { tone: TONE.neutral, text: `Presse ta télécommande ${r.band} MHz maintenant (15 s au plus)…`, running: true };
  try {
    const p = await withRadio(() => api.testReception(r.idx));
    rx.value = { tone: TONE.ok, text: `Réception OK sur la radio ${r.band} MHz : serial ${p.serial}, bouton 0x${p.button}, ${p.rssi} dBm.` };
  } catch (e) {
    rx.value = e === FAILURE.busy ? null : { tone: TONE.danger, text: `Rien capté en 15 s sur la radio ${r.band} MHz. Ce test lit les télécommandes Profalux / KeeLoq : une Somfy ou un portail CAME ne s'y voient pas, même reçus. Sinon : antenne, distance, gain.` };
  }
}
</script>

<template>
  <div class="grid">
    <OpCard title="Diagnostic radio">
      <div class="diag">
        <RadioList v-if="radios.length > 1" :radios="radios" />
        <OpStatline :tone="verdict.tone"><b>{{ verdict.text }}</b></OpStatline>
        <p class="hint mono">{{ summary }}</p>
        <div class="row">
          <OpButton :disabled="testingTx" @click="retestTx">{{ testingTx ? 'Test d\'émission…' : 'Re-tester l\'émission' }}</OpButton>
          <OpButton v-for="r in radios" :key="r.idx" :icon="ICON.remote" :disabled="!!rx?.running" @click="testRx(r)">Tester la réception {{ r.band }} MHz</OpButton>
        </div>
        <OpProgress v-if="rx?.running" label="Test de réception" />
        <OpStatline v-if="rx" :tone="rx.tone">{{ rx.text }}</OpStatline>
        <p class="hint">La réception se teste radio par radio : chaque bouton écoute sa propre puce. Une télécommande Profalux ne se capte pas ? Vérifie la
          bande (868), l'antenne, et le gain dans Système › Appareil.</p>
      </div>
    </OpCard>
    <OpCard title="Capture brute et rejeu" sub="Enregistre le signal d'une télécommande à code fixe que le firmware ne décode pas (portail, capteur), et le rejoue à l'identique. Les codes tournants (Somfy, Profalux) ne se rejouent pas ainsi.">
      <CapturePanel replay />
    </OpCard>
  </div>
</template>

<style scoped>
.diag{display:grid;gap:12px}
.mono{font-family:var(--mono);font-size:.8rem}
</style>
