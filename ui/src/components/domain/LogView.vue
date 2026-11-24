<script setup lang="ts">
// Le journal du firmware (les memes messages que le port serie), relu toutes les 2 s si « Suivre » est actif.
// Copier, telecharger. Les avertissements et erreurs sont colores.
import { computed, nextTick, onMounted, onUnmounted, ref } from 'vue';
import { toast } from '../../store';
import api from '../../api/index';
import { download } from '../../downloads';
import { BUTTON, DOWNLOAD } from '../../domain';
import OpSwitch from '../base/OpSwitch.vue';
import OpButton from '../base/OpButton.vue';

const REFRESH_MS = 2000;
const text = ref('');
const follow = ref(true);
const box = ref<HTMLElement | null>(null);
let timer: ReturnType<typeof setInterval> | undefined;
/* Niveau d'une ligne, pour sa couleur : E (erreur), W (avertissement), le reste. */
const levelOf = (line: string): string => (/^E \(/.test(line) ? 'e' : /^W \(/.test(line) ? 'w' : '');
const lines = computed(() => text.value.split('\n').filter(Boolean).map(line => ({ line, level: levelOf(line) })));
const info = ref('');
async function read(): Promise<void> {
  const b = box.value;
  const atBottom = !b || b.scrollTop + b.clientHeight >= b.scrollHeight - 20;
  const t = await api.log().catch(() => null);
  if (t == null) { info.value = 'Journal indisponible pour le moment.'; return; }
  text.value = t;
  info.value = `${lines.value.length} ligne(s), ${t.length} octets · ${new Date().toLocaleTimeString('fr-FR')}`;
  if (atBottom) { await nextTick(); if (box.value) box.value.scrollTop = box.value.scrollHeight; }
}
onMounted(() => { read(); timer = setInterval(() => { if (follow.value && !document.hidden) read(); }, REFRESH_MS); });
onUnmounted(() => clearInterval(timer));
/* Le presse-papiers du navigateur exige HTTPS ; l'interface du boitier est en HTTP : repli par selection. */
async function copy(): Promise<void> {
  try { await navigator.clipboard.writeText(text.value); toast('Journal copié'); return; } catch { /* repli */ }
  const area = document.createElement('textarea');
  area.value = text.value;
  document.body.appendChild(area);
  area.select();
  const ok = document.execCommand('copy');
  area.remove();
  toast(ok ? 'Journal copié' : 'Copie impossible : utilise « Télécharger »');
}
const save = (): Promise<void> => download(DOWNLOAD.log, 'openprofalux-journal.txt', () => text.value);
</script>

<template>
  <div class="log-view">
    <div class="row">
      <OpSwitch v-model="follow">Suivre en direct</OpSwitch>
      <span class="spacer" />
      <OpButton small :variant="BUTTON.ghost" @click="read">↻ Actualiser</OpButton>
      <OpButton small @click="copy">Copier</OpButton>
      <OpButton small @click="save">Télécharger</OpButton>
    </div>
    <pre ref="box" class="log" tabindex="0" aria-label="Journal du firmware"><span v-for="(x, i) in lines" :key="i" :class="x.level">{{ x.line }}
</span></pre>
    <p class="hint">{{ info }}</p>
  </div>
</template>

<style scoped>
.log-view{display:grid;gap:10px}
.spacer{flex:1}
.log{margin:0;max-height:360px;overflow:auto;padding:12px;border-radius:var(--radius-sm);background:#050a14;color:#c9d6ea;border:1px solid var(--border);font:12.5px/1.55 var(--mono);white-space:pre-wrap;overflow-wrap:anywhere}
.log .w{color:#ffcf6b}
.log .e{color:#ff8a80}
</style>
