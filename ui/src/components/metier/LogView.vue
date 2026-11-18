<script setup>
// Le journal du firmware (les memes messages que le port serie), relu toutes les 2 s si « Suivre » est
// actif. Copier, telecharger. Les avertissements et erreurs sont colores.
import { computed, nextTick, onMounted, onUnmounted, ref } from 'vue';
import { toast } from '../../store.js';
import api from '../../api/index.js';
import { telecharger as telechargerFichier } from '../../fichiers.js';
import OpSwitch from '../base/OpSwitch.vue';
import OpButton from '../base/OpButton.vue';

const texte = ref('');
const suivre = ref(true);
const boite = ref(null);
let minuteur = null;
const lignes = computed(() => texte.value.split('\n').filter(Boolean).map(l => ({ l, n: /^E \(/.test(l) ? 'e' : /^W \(/.test(l) ? 'w' : '' })));
const info = ref('');
async function lire() {
  const enBas = !boite.value || boite.value.scrollTop + boite.value.clientHeight >= boite.value.scrollHeight - 20;
  const t = await api.journal().catch(() => null);
  if (t == null) { info.value = 'Journal indisponible pour le moment.'; return; }
  texte.value = t;
  info.value = `${lignes.value.length} ligne(s), ${t.length} octets · ${new Date().toLocaleTimeString('fr-FR')}`;
  if (enBas) { await nextTick(); if (boite.value) boite.value.scrollTop = boite.value.scrollHeight; }
}
onMounted(() => { lire(); minuteur = setInterval(() => { if (suivre.value && !document.hidden) lire(); }, 2000); });
onUnmounted(() => clearInterval(minuteur));
/* Le presse-papiers du navigateur exige HTTPS ; l'interface du boitier est en HTTP : repli par selection. */
async function copier() {
  try { await navigator.clipboard.writeText(texte.value); toast('Journal copié'); return; } catch { /* repli */ }
  const zone = document.createElement('textarea');
  zone.value = texte.value;
  document.body.appendChild(zone);
  zone.select();
  const ok = document.execCommand('copy');
  zone.remove();
  toast(ok ? 'Journal copié' : 'Copie impossible : utilise « Télécharger »');
}
const telecharger = () => telechargerFichier('journal', 'openprofalux-journal.txt', () => texte.value);
</script>

<template>
  <div class="journal">
    <div class="rangee">
      <OpSwitch v-model="suivre">Suivre en direct</OpSwitch>
      <span class="espace" />
      <OpButton small variant="ghost" @click="lire">↻ Actualiser</OpButton>
      <OpButton small @click="copier">Copier</OpButton>
      <OpButton small @click="telecharger">Télécharger</OpButton>
    </div>
    <pre ref="boite" class="log" tabindex="0" aria-label="Journal du firmware"><span v-for="(x, i) in lignes" :key="i" :class="x.n">{{ x.l }}
</span></pre>
    <p class="hint">{{ info }}</p>
  </div>
</template>

<style scoped>
.journal{display:grid;gap:10px}
.espace{flex:1}
.log{margin:0;max-height:360px;overflow:auto;padding:12px;border-radius:var(--radius-sm);background:#050a14;color:#c9d6ea;border:1px solid var(--border);font:12.5px/1.55 var(--mono);white-space:pre-wrap;overflow-wrap:anywhere}
.log .w{color:#ffcf6b}
.log .e{color:#ff8a80}
</style>
