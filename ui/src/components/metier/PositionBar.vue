<script setup>
// Position d'un volet, comme le firmware la compte : en % de FERMETURE (0 = ouvert en haut, 100 = ferme en
// bas ; seul Home Assistant recoit l'inverse). La partie pleine de la jauge est la partie fermee. Un clic
// choisit une position (meme calcul que l'interface d'avant).
const props = defineProps({
  position: { type: Number, default: null },
  disabled: Boolean,
});
const emit = defineEmits(['choisir']);
function clic(e) {
  if (props.disabled) return;
  const r = e.currentTarget.getBoundingClientRect();
  emit('choisir', Math.round(100 * (e.clientX - r.left) / r.width));
}
function clavier(e) {
  if (props.disabled || props.position == null) return;
  const pas = e.key === 'ArrowRight' ? 10 : e.key === 'ArrowLeft' ? -10 : 0;
  if (pas) { e.preventDefault(); emit('choisir', Math.max(0, Math.min(100, props.position + pas))); }
}
</script>

<template>
  <div class="position">
    <div class="jauge" role="slider" :aria-valuenow="position ?? undefined" aria-valuemin="0" aria-valuemax="100"
         aria-label="Fermeture en % (clic pour choisir)" :tabindex="disabled ? -1 : 0" :class="{ disabled }"
         :style="{ '--p': position ?? 50 }" @click="clic" @keydown="clavier"><span class="rempli" /><span class="poignee" /></div>
    <span class="libelle">{{ position == null ? 'Position inconnue' : position <= 1 ? 'Ouvert' : position >= 99 ? 'Fermé' : `Fermé à ${position} %` }}</span>
  </div>
</template>

<style scoped>
.position{display:grid;gap:6px}
.jauge{position:relative;height:10px;border-radius:999px;background:color-mix(in srgb,var(--muted) 18%,transparent);cursor:pointer}
.rempli{position:absolute;inset:0;width:calc(var(--p)*1%);border-radius:999px;background:linear-gradient(90deg,color-mix(in srgb,var(--accent) 55%,transparent),var(--accent));transition:width .3s linear}
.poignee{position:absolute;top:50%;left:calc(var(--p)*1%);width:18px;height:18px;border-radius:50%;background:#fff;box-shadow:0 2px 6px rgba(0,0,0,.35),0 0 0 3px var(--accent);transform:translate(-50%,-50%);transition:left .3s linear}
.libelle{font-size:.78rem;color:var(--muted);text-align:right;font-variant-numeric:tabular-nums}
.jauge.disabled{opacity:.45;cursor:not-allowed}
</style>
