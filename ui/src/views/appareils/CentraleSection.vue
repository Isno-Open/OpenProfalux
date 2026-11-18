<script setup>
// Centrale : un groupe de volets que l'on ouvre, ferme ou arrete d'un coup, comme une telecommande
// generale. Elle apparait comme un volet, et en cover Home Assistant.
import { computed, ref } from 'vue';
import { etat, rafraichir, toast } from '../../store.js';
import { aVirgule } from '../../profils.js';
import api from '../../api/index.js';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpIcon from '../../components/base/OpIcon.vue';
import ChipPicker from '../../components/metier/ChipPicker.vue';

const volets = computed(() => (etat.statut && etat.statut.volets) || []);
const centrales = computed(() => volets.value.filter(v => v.central));
/* Qui peut etre membre d'une centrale : profiles.json (roles.central.members : appareils simples et zones). */
const role = v => (v.central ? 'central' : v.zone ? 'zone' : 'simple');
const regle = computed(() => (etat.profils && etat.profils.roles && etat.profils.roles.central) || {});
const candidats = computed(() => volets.value.filter(v => (regle.value.members || ['simple', 'zone']).includes(role(v))).map(v => ({ value: v.id, label: v.id })));
const edite = ref('');          // centrale en cours de modification ('' = nouvelle)
const nom = ref('');
const membres = ref([]);
const supprimer = ref('');      // centrale dont on confirme la suppression

function modifier(c) {
  edite.value = c.id; nom.value = c.id;
  membres.value = (c.members || '').split(',').filter(Boolean);
}
function annuler() { edite.value = ''; nom.value = ''; membres.value = []; }
async function enregistrer() {
  const n = nom.value.trim();
  if (!n) return toast('Donne un nom à la centrale');
  if (aVirgule(n)) return toast('La virgule n\'est pas permise dans un nom de volet');
  if (!membres.value.length) return toast('Coche au moins un volet');
  try { await api.centrale({ id: n, members: membres.value }); toast('Centrale enregistrée'); annuler(); rafraichir(); }
  catch (e) { toast(`Échec de l'enregistrement${typeof e === 'string' ? ` : ${e}` : ''}`); }
}
async function confirmerSuppression(id) {
  await api.supprimer(id);
  supprimer.value = '';
  toast('Centrale supprimée');
  rafraichir();
}
</script>

<template>
  <div class="grille">
    <OpCard title="Centrales" sub="Une centrale envoie ouvrir, fermer ou stop à plusieurs volets d'un coup. Elle apparaît comme un volet et dans Home Assistant.">
      <div class="liste">
        <div v-for="c in centrales" :key="c.id" class="ligne">
          <OpIcon name="centrale" size="1.6em" />
          <div class="texte"><b>{{ c.id }}</b><span class="detail">{{ (c.members || '').split(',').filter(Boolean).join(', ') || 'aucun volet' }}</span></div>
          <div v-if="supprimer === c.id" class="rangee">
            <OpButton small @click="supprimer = ''">Annuler</OpButton>
            <OpButton small variant="danger" @click="confirmerSuppression(c.id)">Supprimer</OpButton>
          </div>
          <div v-else class="rangee">
            <OpButton small @click="modifier(c)">Modifier</OpButton>
            <OpButton small variant="danger" :aria-label="`Supprimer la centrale ${c.id}`" @click="supprimer = c.id">Supprimer</OpButton>
          </div>
        </div>
        <p v-if="!centrales.length" class="hint">Aucune centrale pour l'instant.</p>
        <p v-if="supprimer" class="hint">Les volets de la centrale ne sont pas touchés.</p>
      </div>
    </OpCard>

    <OpCard :title="edite ? `Modifier « ${edite} »` : 'Nouvelle centrale'">
      <div class="formulaire">
        <OpField label="Nom" pour="centrale-nom">
          <input id="centrale-nom" v-model="nom" class="op-input" placeholder="ex : Tous les volets" maxlength="23" :disabled="!!edite">
        </OpField>
        <OpField :label="regle.members_label || 'Volets pilotés'">
          <ChipPicker v-model="membres" multiple :options="candidats" label="Volets pilotés" vide="Aucun volet à grouper pour l'instant." />
        </OpField>
        <div class="rangee">
          <OpButton variant="primary" @click="enregistrer">Enregistrer la centrale</OpButton>
          <OpButton v-if="edite || nom || membres.length" variant="ghost" @click="annuler">Annuler</OpButton>
        </div>
      </div>
    </OpCard>
  </div>
</template>

<style scoped>
.liste,.formulaire{display:grid;gap:10px}
.formulaire{gap:14px}
.ligne{display:grid;grid-template-columns:auto 1fr auto;align-items:center;gap:12px;padding:11px 12px;border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surface-2)}
.texte{display:grid;gap:2px;min-width:0}
.detail{font-size:.8rem;color:var(--muted)}
@media (max-width:520px){.ligne{grid-template-columns:auto 1fr}.ligne .rangee{grid-column:1 / -1;justify-content:flex-end}}
</style>
