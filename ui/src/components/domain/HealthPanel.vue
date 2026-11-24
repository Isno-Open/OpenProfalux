<script setup lang="ts">
// L'etat du boitier (/api/health + statut) : allume depuis, dernier redemarrage, memoire, reglages (NVS),
// stockage, Wi-Fi, MQTT, firmware. Une valeur inquietante passe en orange.
import { computed } from 'vue';
import { state } from '../../store';
import type { Health } from '../../api/types';
import SignalBars from './SignalBars.vue';
const props = withDefaults(defineProps<{ health?: Health | null }>(), { health: null });

const WATCHDOG = 'chien de garde (blocage)';
const RESET_REASONS = ['inconnue', 'mise sous tension', 'bouton reset', 'redémarrage logiciel (mise à jour, réglages, retour du Wi-Fi…)', 'plantage', WATCHDOG, WATCHDOG, WATCHDOG,
  'réveil de veille', 'alimentation trop faible', 'SDIO', 'console USB', 'console USB', 'eFuse', 'coupure brève de l\'alimentation', 'plantage'];
const BAD_RESETS = [4, 5, 6, 7, 9, 14, 15];
const LOW_HEAP_BYTES = 20480;
const NVS_NAMESPACES: Record<string, string> = { shutters: 'volets', cfg: 'réglages', pfx_enrol: 'enrôlement', 'nvs.net80211': 'pile Wi-Fi', phy: 'calibration radio' };
const kb = (bytes: number): string => `${Math.round(bytes / 1024)} Ko`;
const duration = (s: number): string => {
  const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  return d ? `${d} j ${h} h` : h ? `${h} h ${m} min` : `${m} min`;
};
interface Line { key: string; value: string; warning?: boolean; gauge?: number; rssi?: number; backtrace?: string }
const lines = computed<Line[]>(() => {
  const h = props.health, s = state.status;
  if (!h) return [];
  const usable = h.nvs_total - (h.nvs_free - h.nvs_avail);
  const pct = usable > 0 ? Math.round(100 * (1 - h.nvs_avail / usable)) : 0;
  const more = h.volets && h.volet_entries ? Math.floor((h.nvs_avail * h.volets) / h.volet_entries) : null;
  const namespaces = Object.entries(h.nvs_ns ?? {}).sort((a, b) => b[1] - a[1]).map(([k, v]) => `${NVS_NAMESPACES[k] ?? k} ${v}`).join(' · ');
  const result: Line[] = [
    { key: 'Allumé depuis', value: s?.uptime != null ? duration(s.uptime) : '…' },
    { key: 'Dernier redémarrage', value: RESET_REASONS[h.reset] ?? `code ${h.reset}`, warning: BAD_RESETS.includes(h.reset) },
    { key: 'Mémoire', value: `${kb(h.heap)} libres · au plus bas ${kb(h.heap_min)} · plus gros bloc ${kb(h.heap_blk)}`, warning: h.heap_min < LOW_HEAP_BYTES },
    { key: 'Réglages (NVS)', value: `${pct} % utilisés${more != null ? ` · environ ${more} volet${more > 1 ? 's' : ''} de plus` : ''}`, gauge: pct, warning: more != null && more < 2 },
    { key: 'NVS par usage', value: `${namespaces} (entrées, sur ${usable} utilisables)` },
    { key: 'Stockage', value: `${kb(h.spiffs_used)} sur ${kb(h.spiffs_total)}`, gauge: Math.round((100 * h.spiffs_used) / h.spiffs_total) },
    { key: 'Wi-Fi', value: s?.wifi ? `« ${s.wifi.ssid} » · ${s.wifi.ip}` : 'non connecté', rssi: s?.wifi?.rssi },
    { key: 'MQTT', value: s?.mqtt ? 'connecté' : 'déconnecté', warning: !s?.mqtt },
    { key: 'Firmware', value: `v${h.version} · partition ${h.part}` },
  ];
  if (h.net_reboots) result.push({ key: 'Garde-fou réseau', value: `${h.net_reboots} redémarrage${h.net_reboots > 1 ? 's' : ''} (passerelle muette)`, warning: true });
  if (h.crash) result.push({ key: 'Dernier plantage', value: `adresse ${h.crash.pc} · cause ${h.crash.cause}`, warning: true, backtrace: h.crash.bt });
  return result;
});
</script>

<template>
  <dl class="health">
    <template v-for="l in lines" :key="l.key">
      <dt>{{ l.key }}</dt>
      <dd :class="{ warning: l.warning }">
        <span>{{ l.value }}</span>
        <SignalBars v-if="l.rssi" :rssi="l.rssi" />
        <span v-if="l.gauge != null" class="gauge" :aria-label="`${l.gauge} %`"><i :style="{ width: `${l.gauge}%` }" /></span>
        <span v-if="l.backtrace" class="backtrace">pile d'appels : <code>{{ l.backtrace }}</code><br>À traduire avec le .elf de cette version :
          <code>xtensa-esp32-elf-addr2line -pfC -e openprofalux.elf</code> suivi des adresses.</span>
      </dd>
    </template>
    <template v-if="!lines.length"><dt>Chargement…</dt><dd /></template>
  </dl>
</template>

<style scoped>
.health{display:grid;grid-template-columns:minmax(150px,auto) 1fr;margin:0;border:1px solid var(--border);border-radius:var(--radius-sm);overflow:hidden}
dt,dd{margin:0;padding:10px 12px;border-bottom:1px solid var(--border)}
dt{font-size:.82rem;color:var(--muted);font-weight:600;background:var(--surface-2)}
dd{display:flex;flex-wrap:wrap;align-items:center;gap:6px 12px;font-size:.88rem}
dt:nth-last-of-type(1),dd:last-child{border-bottom:0}
dd.warning{color:var(--warn)}
.gauge{flex:1 1 100%;max-width:280px;height:5px;border-radius:999px;background:var(--surface-2);border:1px solid var(--border);overflow:hidden}
.gauge i{display:block;height:100%;background:var(--accent)}
.backtrace{flex:1 1 100%;font-size:.78rem;color:var(--muted);overflow-wrap:anywhere}
.backtrace code{font-family:var(--mono);color:var(--text)}
@media (max-width:520px){.health{grid-template-columns:1fr}dt{border-bottom:0;padding-bottom:2px}}
</style>
