<script setup lang="ts">
// Position d'un volet, comme le firmware la compte : en % de FERMETURE (0 = ouvert en haut, 100 = ferme en bas ;
// seul Home Assistant recoit l'inverse). La partie pleine de la jauge est la partie fermee. Un clic choisit une
// position (meme calcul que l'interface d'avant).
const props = withDefaults(defineProps<{ position?: number | null; disabled?: boolean }>(), { position: null });
const emit = defineEmits<{ choose: [position: number] }>();
const STEP_KEYS: Record<string, number> = { ArrowRight: 10, ArrowLeft: -10 };
function onClick(e: MouseEvent): void {
  if (props.disabled || !(e.currentTarget instanceof HTMLElement)) return;
  const r = e.currentTarget.getBoundingClientRect();
  emit('choose', Math.round(100 * (e.clientX - r.left) / r.width));
}
function onKey(e: KeyboardEvent): void {
  if (props.disabled || props.position == null) return;
  const step = STEP_KEYS[e.key] ?? 0;
  if (step) { e.preventDefault(); emit('choose', Math.max(0, Math.min(100, props.position + step))); }
}
</script>

<template>
  <div class="position">
    <div class="gauge" role="slider" :aria-valuenow="position ?? undefined" aria-valuemin="0" aria-valuemax="100"
         aria-label="Fermeture en % (clic pour choisir)" :tabindex="disabled ? -1 : 0" :class="{ disabled }"
         :style="{ '--p': position ?? 50 }" @click="onClick" @keydown="onKey"><span class="fill" /><span class="handle" /></div>
    <span class="label">{{ position == null ? 'Position inconnue' : position <= 1 ? 'Ouvert' : position >= 99 ? 'Fermé' : `Fermé à ${position} %` }}</span>
  </div>
</template>

<style scoped>
.position{display:grid;gap:6px}
.gauge{position:relative;height:10px;border-radius:999px;background:color-mix(in srgb,var(--muted) 18%,transparent);cursor:pointer}
.fill{position:absolute;inset:0;width:calc(var(--p)*1%);border-radius:999px;background:linear-gradient(90deg,color-mix(in srgb,var(--accent) 55%,transparent),var(--accent));transition:width .3s linear}
.handle{position:absolute;top:50%;left:calc(var(--p)*1%);width:18px;height:18px;border-radius:50%;background:#fff;box-shadow:0 2px 6px rgba(0,0,0,.35),0 0 0 3px var(--accent);transform:translate(-50%,-50%);transition:left .3s linear}
.label{font-size:.78rem;color:var(--muted);text-align:right;font-variant-numeric:tabular-nums}
.gauge.disabled{opacity:.45;cursor:not-allowed}
</style>
