<script setup>
// A propos : le projet, ses liens, la version, et les licences de l'interface.
import { onMounted, ref } from 'vue';
import api from '../api/index.js';
import OpCard from '../components/base/OpCard.vue';
import AppLogo from '../components/metier/AppLogo.vue';
const version = ref('');
onMounted(async () => { const s = await api.otaEtat().catch(() => null); version.value = s ? s.version : ''; });
const LIENS = [
  { href: 'https://www.isno.fr/projets/openprofalux', texte: 'Projet et guide sur isno.fr' },
  { href: 'https://www.isno.fr/liste-attente', texte: 'Carte toute faite, sans câblage (liste d\'attente)' },
  { href: 'https://github.com/Isno-Open/OpenProfalux', texte: 'Code source sur GitHub' },
  { href: 'https://github.com/Isno-Open/OpenProfalux/releases', texte: 'Dernières versions' },
];
</script>

<template>
  <OpCard>
    <div class="apropos">
      <div class="entete">
        <AppLogo class="logo" />
        <div><h2>OpenProfalux</h2><p class="hint">Firmware v{{ version || '…' }}</p></div>
      </div>
      <p>Pilotage local des volets Profalux 868 MHz par clonage de télécommande (capture et rejeu), sans clé constructeur.</p>
      <ul class="liens">
        <li v-for="l in LIENS" :key="l.href"><a :href="l.href" target="_blank" rel="noopener">{{ l.texte }}<span aria-hidden="true">↗</span></a></li>
      </ul>
      <p class="hint">© 2026 <a href="https://www.isno.fr" target="_blank" rel="noopener">isno.fr</a>, Olivier (Shad107). Open source.
        Interface construite avec Vue (licence MIT).</p>
    </div>
  </OpCard>
</template>

<style scoped>
.apropos{display:grid;gap:14px}
.entete{display:flex;align-items:center;gap:14px}
.logo{width:52px;height:52px}
p{margin:0}
.liens{list-style:none;margin:0;padding:0;display:grid;gap:8px}
.liens a{display:flex;justify-content:space-between;align-items:center;gap:10px;padding:11px 14px;border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surface-2);color:var(--text);text-decoration:none;font-weight:550}
.liens a:hover{border-color:var(--accent);color:var(--accent)}
</style>
