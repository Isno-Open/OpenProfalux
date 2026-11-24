<script setup lang="ts">
// « Ajouter un appareil » : la pyramide TYPE -> MARQUE -> METHODE, rendue depuis profiles.json (source unique).
// Chaque choix est dans l'adresse (#remotes/add/volet/Profalux) : un F5 ou un lien garde l'etape. Une marque qui
// n'a qu'une methode mene droit a son parcours. On ne montre que les methodes dont la carte a la radio (une carte
// 868 seule ne propose pas le portail 433).
import { computed, onMounted, ref } from 'vue';
import { route, navigate } from '../../router';
import { radios } from '../../store';
import { profiles, types, typeIcon, methodRoute, flowOf, bandLabel, bandColor } from '../../profiles';
import type { Profile } from '../../api/types';
import { SECTION, TAB } from '../../domain';
import OpCard from '../base/OpCard.vue';
import OpIcon from '../base/OpIcon.vue';
import OpSteps from '../base/OpSteps.vue';
import OpBadge from '../base/OpBadge.vue';
import ProfileChoice from './ProfileChoice.vue';
import DetectProposal from './DetectProposal.vue';

const ADD_ROUTE = `${TAB.remotes}/${SECTION.add}`;
const bands = ref<Set<string> | null>(null);
onMounted(async () => { const r = await radios(); bands.value = r ? new Set(r.map(x => String(x.band))) : null; });

/* Profils proposes : ceux qu'une radio de la carte sait emettre, et dont le moteur a un parcours dans cette
   interface (un moteur a venir, io-homecontrol par exemple, n'est pas propose). */
const available = computed(() => profiles().filter(p => flowOf(p) !== null && (!bands.value || bands.value.has(String(p.radio.band)))));
const readPart = (i: number): string | null => { const part = route.rest[i]; return part ? decodeURIComponent(part) : null; };
const type = computed(() => readPart(0));
const brand = computed(() => readPart(1));
const step = computed(() => (!type.value ? 0 : !brand.value ? 1 : 2));
const detecting = ref(false);

/* Un type que la carte ne sait pas piloter reste visible, grise, avec la bande qui le rendrait possible : il dit ce qui
   manque (un portail CAME en 433 sur une carte 868 seule : « disponible en 433 MHz »). */
const typeList = computed(() => Object.entries(types()).map(([key, t]) => ({
  key, label: t.label, count: available.value.filter(p => p.type === key).length,
  bands: [...new Set(profiles().filter(p => p.type === key && p.radio.band).map(p => String(p.radio.band)))],
})));
const typeHint = (t: { count: number; bands: string[] }): string => (t.count ? (t.count > 1 ? `${t.count} méthodes` : '1 méthode')
  : t.bands.length ? `disponible en ${t.bands.map(bandLabel).join(' ou ')}` : 'bientôt');
const brands = computed(() => {
  const list = available.value.filter(p => p.type === type.value);
  return [...new Set(list.map(p => p.brand))].map(name => ({ name, band: list.find(p => p.brand === name)?.radio.band }));
});
const methods = computed(() => available.value.filter(p => p.type === type.value && p.brand === brand.value));
const typeLabel = (key: string | null): string => (key ? types()[key]?.label ?? key : '');

const enc = encodeURIComponent;
function chooseBrand(name: string): void {
  const matching = available.value.filter(p => p.type === type.value && p.brand === name);
  const only = matching.length === 1 ? matching[0] : undefined;
  navigate(only ? methodRoute(only) : `${ADD_ROUTE}/${enc(type.value ?? '')}/${enc(name)}`);
}
const chooseMethod = (p: Profile): void => navigate(methodRoute(p));
</script>

<template>
  <OpCard title="Ajouter un appareil" sub="Choisis le type d'appareil, puis la marque, puis la méthode. On te guide étape par étape.">
    <div class="pyramid">
      <DetectProposal v-if="detecting" :available="available" @cancel="detecting = false" @choose="chooseMethod" />
      <template v-else>
        <OpSteps :steps="['Type', 'Marque', 'Méthode']" :current="step" />
        <nav v-if="step > 0" class="trail" aria-label="Où tu en es">
          <button type="button" class="back-link" @click="navigate(ADD_ROUTE)">‹ Recommencer</button>
          <span>· {{ typeLabel(type) }}</span><span v-if="brand">· {{ brand }}</span>
        </nav>

        <template v-if="step === 0">
          <p class="question">Quel type d'appareil ?</p>
          <div class="tiles">
            <button v-for="t in typeList" :key="t.key" type="button" class="tile" :disabled="!t.count" @click="navigate(`${ADD_ROUTE}/${enc(t.key)}`)">
              <OpIcon :name="typeIcon(t.key)" size="2.6em" />
              <b>{{ t.label }}</b>
              <span class="hint">{{ typeHint(t) }}</span>
            </button>
          </div>
          <p class="hint">Tu ne connais pas la marque ?
            <button type="button" class="link" @click="detecting = true">Détecter en pressant la télécommande</button></p>
        </template>

        <template v-else-if="step === 1">
          <p class="question">Quelle marque ?</p>
          <div class="tiles brands">
            <button v-for="b in brands" :key="b.name" type="button" class="tile brand" :style="b.band ? { '--band': bandColor(b.band) } : undefined"
                    @click="chooseBrand(b.name)">
              <b>{{ b.name }}</b>
              <OpBadge v-if="b.band" class="band">{{ bandLabel(b.band) }}</OpBadge>
            </button>
          </div>
        </template>

        <template v-else>
          <p class="question">Comment l'ajouter ?</p>
          <div class="list">
            <ProfileChoice v-for="p in methods" :key="p.name" :profile="p" @choose="chooseMethod" />
          </div>
        </template>
      </template>
    </div>
  </OpCard>
</template>

<style scoped>
.pyramid{display:grid;gap:14px}
.trail{display:flex;flex-wrap:wrap;align-items:center;gap:6px;font-size:.85rem;color:var(--muted)}
.question{margin:4px 0 0;font-weight:600}
.tiles{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}
.tile{display:grid;justify-items:center;gap:6px;padding:22px 14px;border:1px solid var(--border);border-radius:16px;background:var(--surface-2);color:var(--text);font:inherit;cursor:pointer;transition:.12s}
.tile:hover:not(:disabled){border-color:var(--accent);transform:translateY(-1px)}
.tile:disabled{opacity:.45;cursor:default}
/* Tuile de marque : le nom puis la bande, centres, toutes de meme hauteur ; la couleur de la bande (bandColor) en
   lisere du haut et sur le badge. */
.tile.brand{--band:var(--border);align-content:center;gap:10px;padding:22px 14px;min-height:104px;text-align:center;border-top:3px solid var(--band)}
.tile.brand:hover{border-top-color:var(--band)}
.tile.brand > b{font-size:1.06rem}
.tile.brand .band{color:var(--band);border-color:color-mix(in srgb,var(--band) 45%,var(--border));background:color-mix(in srgb,var(--band) 12%,var(--surface-2))}
.list{display:grid;gap:9px}
.link{background:none;border:0;padding:0;color:var(--accent);font:inherit;font-weight:600;cursor:pointer;text-decoration:underline}
</style>
