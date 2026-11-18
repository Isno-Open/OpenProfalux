<script setup>
// Ecran de connexion : il masque l'application tant que la session n'est pas ouverte.
import { ref, onMounted } from 'vue';
import { connexion } from '../../store.js';
import { fausseDonnees } from '../../api/index.js';
import AppLogo from './AppLogo.vue';
import OpButton from '../base/OpButton.vue';

const pass = ref(''), erreur = ref(false), attente = ref(false), champ = ref(null);
onMounted(() => champ.value && champ.value.focus());
async function valider() {
  erreur.value = false; attente.value = true;
  const ok = await connexion(pass.value);
  attente.value = false;
  if (!ok) { erreur.value = true; champ.value.select(); }
}
</script>

<template>
  <div class="ecran">
    <form class="carte" autocomplete="on" @submit.prevent="valider">
      <AppLogo class="logo-connexion" />
      <h1>OpenProfalux</h1>
      <p class="aide">Interface protégée par mot de passe.</p>
      <input ref="champ" v-model="pass" type="password" autocomplete="current-password" placeholder="Mot de passe"
             aria-label="Mot de passe" required>
      <p v-if="erreur" class="err" role="alert">Mot de passe incorrect.</p>
      <OpButton type="submit" variant="primary" wide :disabled="attente">{{ attente ? 'Connexion…' : 'Se connecter' }}</OpButton>
      <p v-if="fausseDonnees" class="note-maquette">Fausses données : n'importe quel mot de passe, sauf « faux ».</p>
    </form>
  </div>
</template>

<style scoped>
.ecran{min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;background:var(--bg)}
.carte{width:min(340px,100%);display:grid;justify-items:center;gap:14px;text-align:center;background:var(--surface);background-image:var(--surface-grad);border:1px solid var(--border);border-radius:var(--radius);padding:36px 28px;box-shadow:var(--shadow)}
.logo-connexion{width:49px;height:49px}
h1{font-size:1.25rem}
.aide{margin:-6px 0 4px;color:var(--muted);font-size:.86rem}
input{width:100%;text-align:center;padding:10px 12px;background:var(--surface-2);border:1px solid var(--border);border-radius:var(--radius-sm);color:var(--text);font:inherit}
input:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 3px var(--accent-weak)}
.err{margin:0;color:var(--danger);font-size:.86rem}
.note-maquette{margin:2px 0 0;font-size:.72rem;color:var(--muted)}
</style>
