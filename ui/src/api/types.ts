// Les contrats echanges avec le firmware (web_ui.c, shutters.c, ota.c...) : un type par reponse ou par corps
// envoye. Les noms de champs sont ceux du JSON du firmware (des donnees, pas des identifiants : ils restent tels
// quels). Les fausses donnees (mock*.ts) sont verifiees contre ces memes types.
import type { Command, Flag } from '../domain';

/* Refus metier commun a beaucoup de routes : { ok: 0, err }. */
export interface OkReply { ok?: number; err?: string }

export interface Session { protected: boolean; authed: boolean }

/* Une commande apprise : bouton et serial de la telecommande. */
export interface LearnedCommand { b: number; s: string }

/* Un appareil de /api/status (champ volets) : volet, portail, centrale ou zone. */
export interface Device {
  id: string;
  type?: number;
  marque?: string;
  central?: boolean;
  zone?: boolean;
  members?: string;
  follows?: string[];   // les zones que ce volet suit
  order: number;
  orientation?: number;
  position?: number;
  freq_khz?: number;
  te_us?: number;
  tx_rpt?: number;
  tx_rmt?: number;
  frame_bits?: number;
  virt?: boolean;
  virt_serial?: string;
  proto?: string;
  rejeu?: boolean;
  rejeu_cmds?: Partial<Record<Command, boolean>>;
  serials: string[];
  cmd: Partial<Record<Command, LearnedCommand>>;
  travel_up_ms?: number;
  travel_down_ms?: number;
}

export interface RfFrame { serial: string; button: string; rssi: number; hop?: string; t?: number }
export interface WifiStatus { connected: boolean; ssid: string; rssi?: number; ip: string; fallback: boolean }
export interface BatteryState { low: boolean }

export interface Status {
  uptime: number;
  listening: boolean;
  volets: Device[];
  remotes: Record<string, string>;
  remotes_ignored: string[];
  remotes_max?: number;
  remote_battery?: Record<string, BatteryState>;
  rf: RfFrame[];
  wifi?: WifiStatus;
  mqtt?: boolean;
}

/* Corps de /api/shutter. */
export interface ShutterOrder { id: string; cmd: Command; value?: number }
export interface ShutterReply extends OkReply { busy?: boolean }

/* profiles.json : types d'appareil, profils (marque, methode), roles et bornes. */
export interface TypeEntry {
  label: string;
  code: number;
  icon?: string;
  positional?: boolean;
  dpad?: Partial<Record<Command, string>>;
  travel_s?: { min: number; max: number };
  capture?: { cmd: string; label: string }[];
}
export interface ProfileTx { te_us?: number; repeat_bit?: boolean; rmt?: boolean; frame_bits?: number }
export interface Profile {
  nom: string;
  marque: string;
  type: string;          // une cle de types (DEVICE_TYPE)
  mecanisme: string;     // MECHANISM
  flow: string;          // FLOW
  band: string;
  freq_khz?: number;
  description?: string;
  rx?: { datarate_baud?: number; bandwidth_khz?: number };
  tx?: ProfileTx;
}
export interface RoleEntry { label?: string; icon?: string; members_label?: string; members?: string[]; positional_only?: boolean; list_first?: boolean }
export interface Profiles {
  types: Record<string, TypeEntry>;   // cles : DEVICE_TYPE, et celles qu'une version future ajoutera
  profils: Profile[];
  roles?: { central?: RoleEntry; zone?: RoleEntry };
  name_max?: number;
  devices_max?: number;
}

/* /api/radios. */
export interface RadioSummary { idx: number; band: string; chip?: string; freq_khz?: number; ok?: boolean }
export interface RadioDetail {
  idx: number;
  band: string;
  band_low_khz: number;
  band_high_khz: number;
  partnum: number;
  version: number;
  present: boolean;
  primary: boolean;
  radio_profile: string;
  freq_khz: number;
  freq_custom_khz: number;
  rx_gain: number;
  profiles: string[];
}
export interface RadiosReply { active?: number; radios: RadioDetail[] }

/* Detection (/api/capture/last) et captures brutes. */
export interface CaptureLast { n: number; band?: string; freq_khz?: number; rssi?: number; nframes?: number }
export interface ReplayCaptureReply { n: number }
/* Capture d'une commande KeeLoq (/api/learn/poll). */
export interface LearnPoll { bits?: string; serial: string; button: string; rssi: number }
/* Balayage « Caler sur ma telecommande » (/api/rx/sweep). */
export interface Sweep { state: number; status: number; center_khz: number; from_khz: number; peak_dbm: number; floor_dbm: number }
/* Calibration du gain (/api/rx/calibrate). */
export interface GainCalibration { state: number; testing?: number; result?: number }

/* Telecommande virtuelle (/api/pfx). */
export interface PfxModel { name: string; routine: string }
export interface PfxState {
  models: PfxModel[];
  active: Flag;
  learn_ok?: Flag;
  serial?: string;
  idx?: number;
  counter?: number;
  remote?: string;
  model?: number;
  marque?: string;
}
export interface PfxCaptureReply { done: boolean | number; rc: number; new: boolean | number; serial: string; remote: string }
export interface PfxRenewReply extends OkReply { serial?: string; rc?: number }

/* Telecommandes captees. */
export interface DeviceFromRemoteReply { learned?: string }

/* /api/config (lecture) : reglages du boitier et de la radio primaire. */
export interface Config {
  wifi_ssid: string;
  device: string;
  mdns_default?: string;
  mdns_host?: string;
  mqtt_uri: string;
  mqtt_user: string;
  mqtt_pass_len: number;
  mqtt_ca_len: number;
  mqtt_cert_len: number;
  mqtt_key_len: number;
  mqtt_obey?: Flag;      // absent sur un firmware d'avant : oui
  log_frames: Flag;
  debug: Flag;
  ui_auth: Flag;
  update_auto?: Flag;    // absent sur un firmware d'avant : oui
  radio_profile?: string;
  freq_khz?: number;
  rx_gain?: number;
}
/* /api/config (ecriture) : seuls les champs envoyes changent ; radio: N pour une radio secondaire. Les drapeaux
   s'ecrivent en booleen ; ui_auth ne s'ecrit pas (ui_pass). */
type ConfigLength = 'mqtt_pass_len' | 'mqtt_ca_len' | 'mqtt_cert_len' | 'mqtt_key_len';
type ConfigFlag = 'mqtt_obey' | 'log_frames' | 'debug' | 'ui_auth' | 'update_auto';
export const WRITABLE_CONFIG_FLAGS = ['mqtt_obey', 'log_frames', 'debug', 'update_auto'] as const satisfies readonly ConfigFlag[];
export type ConfigUpdate = Partial<Omit<Config, ConfigLength | ConfigFlag>>
  & Partial<Record<(typeof WRITABLE_CONFIG_FLAGS)[number], boolean>> & {
  radio?: number;
  wifi_pass?: string;
  mqtt_pass?: string;
  mqtt_ca?: string;
  mqtt_cert?: string;
  mqtt_key?: string;
  ui_pass?: string;
  reboot?: boolean;
};
export interface ConfigReply extends OkReply { radio_applied?: number }
export interface MqttDiscovery { uri?: string }

/* Cloud ISNO. */
export interface CloudStatus { paired: boolean; enabled: boolean; connected: boolean; name: string; device_id: string }
export interface CloudGrantReply extends OkReply { uri?: string; user_code?: string; status?: number }
/* En attente (pending), le boitier redonne le code et l'adresse : l'ecran reprend l'appairage. */
export interface CloudGrantPoll { state: string; device_id?: string; user_code?: string; uri?: string }
export interface CloudPairReply extends OkReply { device_id?: string }

/* /api/health et /api/diag. */
export interface Crash { pc: string; cause: number; bt: string }
export interface Health {
  reset: number;
  heap: number;
  heap_min: number;
  heap_blk: number;
  nvs_free: number;
  nvs_avail: number;
  nvs_total: number;
  nvs_used: number;
  nvs_ns: Record<string, number>;
  volets: number;
  volet_entries: number;
  spiffs_used: number;
  spiffs_total: number;
  part: string;
  version: string;
  crash?: Crash;
  net_reboots?: number;
}
export interface Diag { partnum: number; version: number; tx_ok: number; gdo0: number; gdo0_lvl: number; spi_retries: number; spi_fails: number }
export interface ReceptionTest { serial: string; button: string; rssi: number }
export interface MultiCaptureReply { nframes?: number }

/* Trames RF et dataset. */
export interface RfPage { total: number; frames: RfFrame[] }
export interface DatasetFrame { hop: string; button: string; t: number }
export interface Dataset { trames: Record<string, { count: number; frames: DatasetFrame[] }> }

/* /api/ota/status. */
export interface OtaStatus {
  version: string;
  part?: string;
  target?: string;
  rb_auto?: number;
  img?: string;
  rb_possible?: Flag;
  precedente?: string;
  annulee?: string;
  annulee_auto?: number;
  latest: string;
  latest_url: string;
  checks: number;
  checking?: number;
  check_waiting?: number;
  state: number;
  written: number;
  total: number;
  msg: string;
}
export interface OtaInstallReply { rebooting: boolean }

/* Sauvegarde. */
export interface UndoInfo { available: boolean; t?: number }

/* Support. */
export interface ReportBody { email: string; brand: string; model: string; purpose: string; note: string }
export interface ReportReply extends OkReply { status?: number }   // status : reponse du relais
