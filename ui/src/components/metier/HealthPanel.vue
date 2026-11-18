<script setup>
// L'etat du boitier (/api/health + statut) : allume depuis, dernier redemarrage, memoire, reglages
// (NVS), stockage, Wi-Fi, MQTT, firmware. Une valeur inquietante passe en orange.
import { computed } from 'vue';
import { etat } from '../../store.js';
import SignalBars from './SignalBars.vue';
const props = defineProps({ sante: { type: Object, default: null } });

const WDT = 'chien de garde (blocage)';
const RESET = ['inconnue', 'mise sous tension', 'bouton reset', 'redémarrage logiciel (mise à jour, réglages, retour du Wi-Fi…)', 'plantage', WDT, WDT, WDT,
  'réveil de veille', 'alimentation trop faible', 'SDIO', 'console USB', 'console USB', 'eFuse', 'coupure brève de l\'alimentation', 'plantage'];
const RESET_MAUVAIS = [4, 5, 6, 7, 9, 14, 15];
const NS = { shutters: 'volets', cfg: 'réglages', pfx_enrol: 'enrôlement', 'nvs.net80211': 'pile Wi-Fi', phy: 'calibration radio' };
const ko = b => `${Math.round(b / 1024)} Ko`;
const duree = s => {
  const j = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  return j ? `${j} j ${h} h` : h ? `${h} h ${m} min` : `${m} min`;
};
const lignes = computed(() => {
  const h = props.sante, s = etat.statut || {};
  if (!h) return [];
  const utilisables = h.nvs_total - (h.nvs_free - h.nvs_avail);
  const pct = utilisables > 0 ? Math.round(100 * (1 - h.nvs_avail / utilisables)) : 0;
  const plus = h.volets && h.volet_entries ? Math.floor((h.nvs_avail * h.volets) / h.volet_entries) : null;
  const ns = Object.entries(h.nvs_ns || {}).sort((a, b) => b[1] - a[1]).map(([k, v]) => `${NS[k] || k} ${v}`).join(' · ');
  return [
    { k: 'Allumé depuis', v: s.uptime != null ? duree(s.uptime) : '…' },
    { k: 'Dernier redémarrage', v: RESET[h.reset] || `code ${h.reset}`, alerte: RESET_MAUVAIS.includes(h.reset) },
    { k: 'Mémoire', v: `${ko(h.heap)} libres · au plus bas ${ko(h.heap_min)} · plus gros bloc ${ko(h.heap_blk)}`, alerte: h.heap_min < 20480 },
    { k: 'Réglages (NVS)', v: `${pct} % utilisés${plus != null ? ` · environ ${plus} volet${plus > 1 ? 's' : ''} de plus` : ''}`, jauge: pct, alerte: plus != null && plus < 2 },
    { k: 'NVS par usage', v: `${ns} (entrées, sur ${utilisables} utilisables)` },
    { k: 'Stockage', v: `${ko(h.spiffs_used)} sur ${ko(h.spiffs_total)}`, jauge: Math.round((100 * h.spiffs_used) / h.spiffs_total) },
    { k: 'Wi-Fi', v: s.wifi ? `« ${s.wifi.ssid} » · ${s.wifi.ip}` : 'non connecté', rssi: s.wifi && s.wifi.rssi },
    { k: 'MQTT', v: s.mqtt ? 'connecté' : 'déconnecté', alerte: !s.mqtt },
    { k: 'Firmware', v: `v${h.version} · partition ${h.part}` },
    ...(h.net_reboots ? [{ k: 'Garde-fou réseau', v: `${h.net_reboots} redémarrage${h.net_reboots > 1 ? 's' : ''} (passerelle muette)`, alerte: true }] : []),
    ...(h.crash ? [{ k: 'Dernier plantage', v: `adresse ${h.crash.pc} · cause ${h.crash.cause}`, alerte: true, pile: h.crash.bt }] : []),
  ];
});
</script>

<template>
  <dl class="sante">
    <template v-for="l in lignes" :key="l.k">
      <dt>{{ l.k }}</dt>
      <dd :class="{ alerte: l.alerte }">
        <span>{{ l.v }}</span>
        <SignalBars v-if="l.rssi" :rssi="l.rssi" />
        <span v-if="l.jauge != null" class="jauge" :aria-label="`${l.jauge} %`"><i :style="{ width: `${l.jauge}%` }" /></span>
        <span v-if="l.pile" class="pile">pile d'appels : <code>{{ l.pile }}</code><br>À traduire avec le .elf de cette version :
          <code>xtensa-esp32-elf-addr2line -pfC -e openprofalux.elf</code> suivi des adresses.</span>
      </dd>
    </template>
    <template v-if="!lignes.length"><dt>Chargement…</dt><dd /></template>
  </dl>
</template>

<style scoped>
.sante{display:grid;grid-template-columns:minmax(150px,auto) 1fr;margin:0;border:1px solid var(--border);border-radius:var(--radius-sm);overflow:hidden}
dt,dd{margin:0;padding:10px 12px;border-bottom:1px solid var(--border)}
dt{font-size:.82rem;color:var(--muted);font-weight:600;background:var(--surface-2)}
dd{display:flex;flex-wrap:wrap;align-items:center;gap:6px 12px;font-size:.88rem}
dt:nth-last-of-type(1),dd:last-child{border-bottom:0}
dd.alerte{color:var(--warn)}
.jauge{flex:1 1 100%;max-width:280px;height:5px;border-radius:999px;background:var(--surface-2);border:1px solid var(--border);overflow:hidden}
.jauge i{display:block;height:100%;background:var(--accent)}
.pile{flex:1 1 100%;font-size:.78rem;color:var(--muted);overflow-wrap:anywhere}
.pile code{font-family:var(--mono);color:var(--text)}
@media (max-width:520px){.sante{grid-template-columns:1fr}dt{border-bottom:0;padding-bottom:2px}}
</style>
