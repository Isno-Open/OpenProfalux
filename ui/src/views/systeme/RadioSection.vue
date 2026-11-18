<script setup>
// Systeme › Radio : le diagnostic de chaque puce (detectee, emission, GDO0, SPI), un test de reception par
// radio, et la capture brute avec rejeu.
import { computed, onMounted, ref } from 'vue';
import { avecRadio } from '../../store.js';
import api from '../../api/index.js';
import OpCard from '../../components/base/OpCard.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import OpProgress from '../../components/base/OpProgress.vue';
import RadioList from '../../components/metier/RadioList.vue';
import CapturePanel from '../../components/metier/CapturePanel.vue';

const radios = ref([]);
const d = ref(null);
const testTx = ref(false);
const rx = ref(null);   // { ton, texte, cours }
onMounted(async () => { radios.value = (await api.radiosDetail()).radios || []; d.value = await api.diag().catch(() => null); });
const hex = n => `0x${Number(n).toString(16).toUpperCase().padStart(2, '0')}`;
const verdict = computed(() => {
  const x = d.value;
  if (!x) return { ton: 'neutre', texte: 'Chargement…' };
  if (!(x.partnum === 0 && x.version !== 0 && x.version !== 255)) return { ton: 'danger', texte: `Module non détecté (câblage, SPI). VERSION ${hex(x.version)}` };
  if (x.spi_fails > 0) return { ton: 'danger', texte: 'Réglages radio non pris par le module : coupe l\'alimentation puis redémarre.' };
  if (x.tx_ok !== 1) return { ton: 'danger', texte: 'Détecté, mais l\'émission ne marche pas.' };
  if (x.gdo0 === 0) return { ton: 'danger', texte: 'GDO0 tenu par un autre composant : l\'émission n\'atteint pas le module.' };
  return { ton: 'ok', texte: 'Module OK : détecté, et l\'émission marche.' };
});
const ligne = computed(() => {
  const x = d.value;
  if (!x) return '';
  return `PARTNUM ${hex(x.partnum)} · VERSION ${hex(x.version)} · TX ${x.tx_ok === 1 ? 'OK' : x.tx_ok === 0 ? 'HS' : 'non testé'}`
    + (x.gdo0 === 1 ? ' · GDO0 libre' : x.gdo0 === 0 ? ` · GDO0 bloqué à ${x.gdo0_lvl}` : '')
    + (x.spi_retries || x.spi_fails ? ` · SPI : ${x.spi_retries} reprise(s), ${x.spi_fails} échec(s)` : '');
});
async function retesterTx() {
  testTx.value = true;
  await avecRadio(() => api.diagTx()).catch(() => {});
  d.value = await api.diag().catch(() => d.value);
  testTx.value = false;
}
async function testerRx(r) {
  rx.value = { ton: 'neutre', texte: `Presse ta télécommande ${r.band} MHz maintenant (15 s au plus)…`, cours: true };
  try {
    const p = await avecRadio(() => api.testerReception(r.idx));
    rx.value = { ton: 'ok', texte: `Réception OK sur la radio ${r.band} MHz : serial ${p.serial}, bouton 0x${p.button}, ${p.rssi} dBm.` };
  } catch (e) {
    rx.value = e === 'occupee' ? null : { ton: 'danger', texte: `Rien capté en 15 s sur la radio ${r.band} MHz. Ce test lit les télécommandes Profalux / KeeLoq : une Somfy ou un portail CAME ne s'y voient pas, même reçus. Sinon : antenne, distance, gain.` };
  }
}
</script>

<template>
  <div class="grille">
    <OpCard title="Diagnostic radio">
      <div class="diag">
        <RadioList v-if="radios.length > 1" :radios="radios" />
        <OpStatline :tone="verdict.ton"><b>{{ verdict.texte }}</b></OpStatline>
        <p class="hint mono">{{ ligne }}</p>
        <div class="rangee">
          <OpButton :disabled="testTx" @click="retesterTx">{{ testTx ? 'Test d\'émission…' : 'Re-tester l\'émission' }}</OpButton>
          <OpButton v-for="r in radios" :key="r.idx" icon="telec" :disabled="rx && rx.cours" @click="testerRx(r)">Tester la réception {{ r.band }} MHz</OpButton>
        </div>
        <OpProgress v-if="rx && rx.cours" label="Test de réception" />
        <OpStatline v-if="rx" :tone="rx.ton">{{ rx.texte }}</OpStatline>
        <p class="hint">La réception se teste radio par radio : chaque bouton écoute sa propre puce. Une télécommande Profalux ne se capte pas ? Vérifie la
          bande (868), l'antenne, et le gain dans Système › Appareil.</p>
      </div>
    </OpCard>
    <OpCard title="Capture brute et rejeu" sub="Enregistre le signal d'une télécommande à code fixe que le firmware ne décode pas (portail, capteur), et le rejoue à l'identique. Les codes tournants (Somfy, Profalux) ne se rejouent pas ainsi.">
      <CapturePanel rejeu />
    </OpCard>
  </div>
</template>

<style scoped>
.diag{display:grid;gap:12px}
.mono{font-family:var(--mono);font-size:.8rem}
</style>
