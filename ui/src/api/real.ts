// Les routes du firmware (web_ui.c) pour la session, le pilotage et les appareils. Memes appels et memes
// corps que l'interface d'avant (app.js) : le firmware n'a rien a changer.
import { FAILURE, HTTP_UNAUTHORIZED, SCAN_STATE, type Command } from '../domain';
import { json, post, requireOk, poll } from './http';
import type {
  CaptureLast, DeviceFromRemoteReply, LearnPoll, OkReply, PfxCaptureReply, PfxRenewReply, PfxState, Profiles, RadioSummary,
  ReplayCaptureReply, Session, ShutterOrder, ShutterReply, Status, Sweep,
} from './types';

/* Rejet d'une capture : une autre telecommande que celle de l'appareil a ete entendue. */
export interface OtherRemote { otherRemote: string }
export const isOtherRemote = (e: unknown): e is OtherRemote => typeof e === 'object' && e !== null && 'otherRemote' in e;

export interface CaptureRequest {
  id: string;
  action: Command;
  replay: boolean;
  type: number;
  brand?: string;
  radio: number;
  freq_khz: number;
  rx?: { datarate_baud?: number; bandwidth_khz?: number };
  remoteName: string;
  exists: boolean;
  knownSerials?: string[];
  virtual: boolean;
}
/* Une commande apprise. remoteNameRefused : le nom donne a la telecommande n'a pas ete enregistre (liste des
   telecommandes pleine), la trame l'a ete. */
export interface LearnedCapture extends LearnPoll { remoteNameRefused?: boolean }
export type CaptureResult = ReplayCaptureReply | LearnedCapture;

/* Les routes des telecommandes captees repondent par un texte quand elles refusent : on le garde pour le
   message (« Refuse : ... »). */
async function postWithText<T>(url: string, body: unknown): Promise<T> {
  const r = await fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
  if (!r.ok) return Promise.reject(await r.text().catch(() => String(r.status)));
  const parsed: Promise<T> = r.json().catch(() => ({}));
  return parsed;
}

export default {
  session: () => json<Session>('/api/session'),
  login: (pass: string) => fetch('/api/login', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ pass }) })
    .then(r => r.ok, () => false),
  logout: (): Promise<unknown> => fetch('/api/logout', { method: 'POST' }),
  status: (signal?: AbortSignal) => json<Status>('/api/status', { signal }),
  profiles: (signal?: AbortSignal) => json<Profiles>('/profiles.json', { signal }),
  sendShutter: (order: ShutterOrder) => post<ShutterReply>('/api/shutter', order),
  saveDisplayOrder: (ids: string[]) => requireOk(post('/api/volet/order', { ids })),

  /* ── Appareils ── */
  radios: () => json<{ radios?: RadioSummary[] }>('/api/radios').then(d => d.radios || []),
  /* Detection : la radio ecoute (~15 s, la requete attend la fin), puis on lit ce qu'elle a entendu. */
  listen: async (radio: number) => { await post(`/api/capture/start?radio=${radio}`).catch(() => null); return json<CaptureLast>('/api/capture/last'); },
  /* Capture d'une commande : rejeu brut (CAME, code fixe) ou clone KeeLoq. Rejets : FAILURE.nothing (rien
     capte), OtherRemote (une autre telecommande que celle de l'appareil). */
  async capture(req: CaptureRequest): Promise<CaptureResult> {
    const { id, action, replay, type, radio, freq_khz, rx, remoteName, exists, knownSerials = [], virtual } = req;
    if (replay) {
      if (!exists) await requireOk(post('/api/rejeu/create', { id, type })).catch(() => Promise.reject(FAILURE.nothing));
      const body: Record<string, unknown> = { id, cmd: action, radio };
      if (freq_khz) body.freq_khz = freq_khz;
      if (rx && typeof rx.datarate_baud === 'number' && typeof rx.bandwidth_khz === 'number') {
        body.datarate_baud = rx.datarate_baud;
        body.bandwidth_khz = rx.bandwidth_khz;
      }
      const d = await post<ReplayCaptureReply>('/api/rejeu/capture', body).catch(() => null);
      return d && d.n > 0 ? d : Promise.reject(FAILURE.nothing);
    }
    await post(`/api/learn/start?radio=${radio}${freq_khz ? `&freq_khz=${freq_khz}` : ''}`, { action });
    const r = await poll<LearnPoll>('/api/learn/poll', d => !!d.bits, 40, 400)
      .catch(e => Promise.reject(e === HTTP_UNAUTHORIZED ? HTTP_UNAUTHORIZED : FAILURE.nothing));
    /* Un volet clone rejoue les trames d'UNE telecommande ; un volet virtuel en suit plusieurs. */
    if (!virtual && knownSerials.length && !knownSerials.includes(r.serial)) return Promise.reject({ otherRemote: r.serial } satisfies OtherRemote);
    /* Le nom est facultatif : refuse (liste des telecommandes pleine, {"ok":0} en HTTP 200) ou perdu, il ne fait pas
       echouer la capture ; l'ecran le dit. */
    const named = !remoteName || await requireOk(post('/api/remote', { serial: r.serial, name: remoteName })).then(() => true, () => false);
    /* Refus du boitier (liste des appareils pleine) : rien n'est enregistre, la capture n'a pas abouti. */
    await requireOk(post('/api/learn/assign', { id, action, bits: r.bits }));
    return named ? r : { ...r, remoteNameRefused: true };
  },
  moveCapture: ({ id, from, to }: { id: string; from: Command; to: Command }) => requireOk(post('/api/learn/reassign', { id, from, to })),
  /* « Caler sur ma telecommande » : balayage autour de la porteuse ; rend { status, center_khz, from_khz,
     peak_dbm, floor_dbm }, status SWEEP_STATUS. */
  async tuneToRemote(radio: number): Promise<Sweep> {
    const r = await post<OkReply>(`/api/rx/sweep?radio=${radio}`).catch(() => null);
    if (r && r.ok === 0) return Promise.reject(FAILURE.start);
    return poll<Sweep>(`/api/rx/sweep?radio=${radio}`, x => x.state === SCAN_STATE.done);
  },
  saveTxParams: (body: Record<string, unknown>) => requireOk(post('/api/volet/txparams', body)),
  rename: (id: string, name: string) => requireOk(post('/api/volet/rename', { id, name })),
  setType: (id: string, type: number) => requireOk(post('/api/volet/type', { id, type })),
  deleteDevice: (id: string) => requireOk(post('/api/volet/delete', { id })),
  setOrientation: (id: string, value: number) => requireOk(post('/api/volet/orientation', { id, orientation: value })),
  setFollowers: (id: string, members: string[]) => requireOk(post('/api/volet/followers', { id, members })),

  /* Telecommande virtuelle (enrolement) */
  pfxState: () => json<PfxState>('/api/pfx'),
  async pfxCapture(body: Record<string, unknown>): Promise<PfxCaptureReply> {
    const r = await post<OkReply>('/api/pfx/capture', body).catch(() => null);
    if (!r || !r.ok) return Promise.reject(FAILURE.start);
    return poll<PfxCaptureReply>('/api/pfx/capture/poll', d => !!d.done, 20, 1000);
  },
  pfxRenew: () => post<PfxRenewReply>('/api/pfx/renew'),
  pfxLearn: (body: Record<string, unknown>) => post('/api/pfx/learn', body),
  pfxCommand: (cmd: Command) => requireOk(post('/api/pfx/cmd', { cmd })),
  pfxSave: (id: string) => requireOk(post('/api/pfx/save_volet', { id })),
  pfxForget: () => post('/api/pfx/forget'),

  somfyCreate: (body: { id: string; address: string; [key: string]: unknown }) => requireOk(post('/api/somfy/create', body)),
  saveCentral: ({ id, members }: { id: string; members: string[] }) => requireOk(post('/api/central', { id, members })),
  calibrate: (body: { id: string; travel_up_ms: number; travel_down_ms: number }) => requireOk(post('/api/calibrate', body)),

  /* Telecommandes captees */
  remoteRename: (serial: string, name: string) => requireOk(post('/api/remote', { serial, name })),
  remoteDelete: (serial: string) => postWithText<OkReply>('/api/remote/delete', { serial }),
  remoteIgnore: (serial: string) => postWithText<OkReply>('/api/remote/ignore', { serial }),
  remoteUnignore: (serial: string) => requireOk(post('/api/remote/unignore', { serial })),
  deviceFromRemote: (serial: string, name: string) => postWithText<DeviceFromRemoteReply>('/api/remote/create_volet', { serial, name }),
  attachRemote: (serial: string, id: string) => postWithText<DeviceFromRemoteReply>('/api/remote/attach', { serial, id }),
};
