<script setup lang="ts">
// Ecran de connexion : il masque l'application tant que la session n'est pas ouverte.
import { ref, onMounted } from 'vue';
import { login } from '../../store';
import { usesMockData } from '../../api/index';
import { BUTTON } from '../../domain';
import AppLogo from './AppLogo.vue';
import OpButton from '../base/OpButton.vue';

const pass = ref(''), error = ref(false), waiting = ref(false), field = ref<HTMLInputElement | null>(null);
onMounted(() => field.value?.focus());
async function submit(): Promise<void> {
  error.value = false; waiting.value = true;
  const ok = await login(pass.value);
  waiting.value = false;
  if (!ok) { error.value = true; field.value?.select(); }
}
</script>

<template>
  <div class="screen">
    <form class="card" autocomplete="on" @submit.prevent="submit">
      <AppLogo class="login-logo" />
      <h1>OpenProfalux</h1>
      <p class="help">Interface protégée par mot de passe.</p>
      <input ref="field" v-model="pass" type="password" autocomplete="current-password" placeholder="Mot de passe"
             aria-label="Mot de passe" required>
      <p v-if="error" class="err" role="alert">Mot de passe incorrect.</p>
      <OpButton type="submit" :variant="BUTTON.primary" wide :disabled="waiting">{{ waiting ? 'Connexion…' : 'Se connecter' }}</OpButton>
      <p v-if="usesMockData" class="mock-note">Fausses données : n'importe quel mot de passe, sauf « faux ».</p>
    </form>
  </div>
</template>

<style scoped>
.screen{min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;background:var(--bg)}
.card{width:min(340px,100%);display:grid;justify-items:center;gap:14px;text-align:center;background:var(--surface);background-image:var(--surface-grad);border:1px solid var(--border);border-radius:var(--radius);padding:36px 28px;box-shadow:var(--shadow)}
.login-logo{width:49px;height:49px}
h1{font-size:1.25rem}
.help{margin:-6px 0 4px;color:var(--muted);font-size:.86rem}
input{width:100%;text-align:center;padding:10px 12px;background:var(--surface-2);border:1px solid var(--border);border-radius:var(--radius-sm);color:var(--text);font:inherit}
input:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 3px var(--accent-weak)}
.err{margin:0;color:var(--danger);font-size:.86rem}
.mock-note{margin:2px 0 0;font-size:.72rem;color:var(--muted)}
</style>
