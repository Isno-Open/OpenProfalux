<script setup lang="ts">
// Centrale : un groupe de volets que l'on ouvre, ferme ou arrete d'un coup, comme une telecommande generale. Elle
// apparait comme un volet, et en cover Home Assistant.
import { computed, ref } from 'vue';
import { state, refresh, toast } from '../../store';
import { hasComma } from '../../profiles';
import api from '../../api/index';
import type { Device } from '../../api/types';
import { BUTTON, ROLE, type Role } from '../../domain';
import { ICON } from '../../icons';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpIcon from '../../components/base/OpIcon.vue';
import ChipPicker from '../../components/domain/ChipPicker.vue';

const devices = computed(() => state.status?.volets ?? []);
const centrals = computed(() => devices.value.filter(d => d.central));
/* Qui peut etre membre d'une centrale : profiles.json (roles.central.members : appareils simples et zones). */
const roleOf = (d: Device): Role => (d.central ? ROLE.central : d.zone ? ROLE.zone : ROLE.simple);
const rule = computed(() => state.profiles?.roles?.central ?? {});
const allowedRoles = computed<string[]>(() => rule.value.members ?? [ROLE.simple, ROLE.zone]);
const candidates = computed(() => devices.value.filter(d => allowedRoles.value.includes(roleOf(d))).map(d => ({ value: d.id, label: d.id })));
const editing = ref('');          // centrale en cours de modification ('' = nouvelle)
const name = ref('');
const members = ref<string | string[]>([]);
const deleting = ref('');         // centrale dont on confirme la suppression
const memberList = (): string[] => (Array.isArray(members.value) ? members.value : []);

function edit(c: Device): void {
  editing.value = c.id; name.value = c.id;
  members.value = (c.members || '').split(',').filter(Boolean);
}
function cancel(): void { editing.value = ''; name.value = ''; members.value = []; }
async function save(): Promise<void> {
  const n = name.value.trim();
  if (!n) { toast('Donne un nom à la centrale'); return; }
  if (hasComma(n)) { toast('La virgule n\'est pas permise dans un nom de volet'); return; }
  if (!memberList().length) { toast('Coche au moins un volet'); return; }
  try { await api.saveCentral({ id: n, members: memberList() }); toast('Centrale enregistrée'); cancel(); refresh(); }
  catch (e) { toast(`Échec de l'enregistrement${typeof e === 'string' ? ` : ${e}` : ''}`); }
}
async function confirmDelete(id: string): Promise<void> {
  await api.deleteDevice(id);
  deleting.value = '';
  toast('Centrale supprimée');
  refresh();
}
</script>

<template>
  <div class="grid">
    <OpCard title="Centrales" sub="Une centrale envoie ouvrir, fermer ou stop à plusieurs volets d'un coup. Elle apparaît comme un volet et dans Home Assistant.">
      <div class="list">
        <div v-for="c in centrals" :key="c.id" class="item">
          <OpIcon :name="ICON.central" size="1.6em" />
          <div class="text"><b>{{ c.id }}</b><span class="detail">{{ (c.members || '').split(',').filter(Boolean).join(', ') || 'aucun volet' }}</span></div>
          <div v-if="deleting === c.id" class="row">
            <OpButton small @click="deleting = ''">Annuler</OpButton>
            <OpButton small :variant="BUTTON.danger" @click="confirmDelete(c.id)">Supprimer</OpButton>
          </div>
          <div v-else class="row">
            <OpButton small @click="edit(c)">Modifier</OpButton>
            <OpButton small :variant="BUTTON.danger" :aria-label="`Supprimer la centrale ${c.id}`" @click="deleting = c.id">Supprimer</OpButton>
          </div>
        </div>
        <p v-if="!centrals.length" class="hint">Aucune centrale pour l'instant.</p>
        <p v-if="deleting" class="hint">Les volets de la centrale ne sont pas touchés.</p>
      </div>
    </OpCard>

    <OpCard :title="editing ? `Modifier « ${editing} »` : 'Nouvelle centrale'">
      <div class="form">
        <OpField label="Nom" control="central-name">
          <input id="central-name" v-model="name" class="op-input" placeholder="ex : Tous les volets" :maxlength="state.profiles?.name_max || 23" :disabled="!!editing">
        </OpField>
        <OpField :label="rule.members_label || 'Volets pilotés'">
          <ChipPicker v-model="members" multiple :options="candidates" label="Volets pilotés" empty="Aucun volet à grouper pour l'instant." />
        </OpField>
        <div class="row">
          <OpButton :variant="BUTTON.primary" @click="save">Enregistrer la centrale</OpButton>
          <OpButton v-if="editing || name || memberList().length" :variant="BUTTON.ghost" @click="cancel">Annuler</OpButton>
        </div>
      </div>
    </OpCard>
  </div>
</template>

<style scoped>
.list,.form{display:grid;gap:10px}
.form{gap:14px}
.item{display:grid;grid-template-columns:auto 1fr auto;align-items:center;gap:12px;padding:11px 12px;border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surface-2)}
.text{display:grid;gap:2px;min-width:0}
.detail{font-size:.8rem;color:var(--muted)}
@media (max-width:520px){.item{grid-template-columns:auto 1fr}.item .row{grid-column:1 / -1;justify-content:flex-end}}
</style>
