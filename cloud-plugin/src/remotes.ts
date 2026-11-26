// Telecommandes connues d'un boitier OpenProfalux (docs/CLOUD.md, point 7) : on les gere depuis la page
// du boitier, sans les piloter. Ce que publie le firmware (shutters.c remotes_sync) :
//   remote/<slug>/info   {"serial":"0x0000813","name":"...","used_by":"Chambre Parents"} (retenu ; vide = retiree)
//   ignored/<slug>       {"serial":"0x0000813"} (retenu ; vide = reacceptee)
// et ce qu'il accepte : remote/<slug>/set DELETE|IGNORE, remote/<slug>/config {"name"}, ignored/<slug>/set
// ACCEPT. Le slug est le serial sans « 0x ». Supprimer ou ignorer une telecommande qu'un volet utilise
// est refuse par le boitier : le portail ne propose pas ces boutons dans ce cas.
import { REMOTE_ICON } from "./kinds.generated.js";
import { CONTROL, FACT, KIND_PLACE, type Fact, type FamilyCommand, type KindDecl } from "../opencloudapp/server/src/family-contract.js";
import { SETTING_KIND, TEXT_FORMAT } from "../opencloudapp/server/src/plugin-contract.js";
import { NAME_MAX } from "./kinds.generated.js";

export const REMOTE_KIND = { known: "remote", ignored: "remote-ignored" } as const;
export const REMOTE_CONTROL = { remove: "remove", ignore: "ignore", accept: "accept", usedBy: "used_by" } as const;
/** Reglage : le nom de la telecommande (cle propre, pour ne pas la confondre avec un reglage de volet). */
export const REMOTE_NAME = "remote_name";
/** Un appareil « ignoree » a son propre identifiant : la meme telecommande ne peut etre les deux a la fois,
 *  mais le passage de l'un a l'autre ne doit pas melanger leurs etats. */
const IGNORED_PREFIX = "ignored-";

const GROUP = { fr: "Télécommandes", en: "Remotes" };
const ICON = REMOTE_ICON;

export const REMOTE_KINDS = [
  { id: REMOTE_KIND.known, label: { fr: "Télécommande", en: "Remote" }, icon: ICON, place: KIND_PLACE.box, group: GROUP,
    hint: {
      fr: "Les télécommandes que le boîtier connaît : celles appairées à un volet, et celles qu'il a captées à la radio. Une carte sans « Utilisée par » n'est liée à aucun volet, ce peut être celle d'un voisin. Supprimer la retire de la liste (elle revient si le boîtier la capte de nouveau) ; Ignorer fait que le boîtier ne l'enregistre plus. La roue crantée sert à lui donner un nom ; sans nom, elle s'affiche sous son numéro de série.",
      en: "The remotes the box knows: those paired with a shutter, and those it picked up over the air. A card without “Used by” is linked to no shutter and may be a neighbour's. Delete removes it from the list (it comes back if the box picks it up again); Ignore makes the box stop recording it. The gear gives it a name; without one, it shows its serial number.",
    },
    controls: [
      { kind: CONTROL.reading, id: REMOTE_CONTROL.usedBy, label: { fr: "Utilisée par", en: "Used by" } },
      { kind: CONTROL.action, id: REMOTE_CONTROL.remove, label: { fr: "Supprimer", en: "Delete" },
        confirm: { fr: "Supprimer cette télécommande de la liste ? Elle reviendra si le boîtier la capte de nouveau.", en: "Remove this remote from the list? It comes back if the box picks it up again." } },
      { kind: CONTROL.action, id: REMOTE_CONTROL.ignore, label: { fr: "Ignorer", en: "Ignore" },
        confirm: { fr: "Ignorer cette télécommande ? Le boîtier ne l'enregistrera plus, jusqu'à ce qu'on la réaccepte.", en: "Ignore this remote? The box will no longer record it until it is accepted again." } },
    ],
    settings: [{ kind: SETTING_KIND.text, key: REMOTE_NAME, label: { fr: "Nom", en: "Name" }, format: TEXT_FORMAT.plain, maxLength: NAME_MAX }] },
  { id: REMOTE_KIND.ignored, label: { fr: "Télécommande ignorée", en: "Ignored remote" }, icon: ICON, place: KIND_PLACE.box, group: GROUP,
    controls: [{ kind: CONTROL.action, id: REMOTE_CONTROL.accept, label: { fr: "Réaccepter", en: "Accept again" } }] },
] satisfies readonly KindDecl[];

const json = (s: string): Record<string, unknown> | null => {
  try { const v: unknown = JSON.parse(s); return v && typeof v === "object" && !Array.isArray(v) ? (v as Record<string, unknown>) : null; }
  catch { return null; }
};
const text = (v: unknown): string | null => (typeof v === "string" && v !== "" ? v : null);

/** Messages des telecommandes -> faits ; null si le chemin n'en est pas un. */
export function readRemote(path: string, payload: string): Fact[] | null {
  let m = /^remote\/([^/]+)\/info$/.exec(path);
  if (m) {
    const device = m[1]!;
    if (payload.trim() === "") return [{ fact: FACT.gone, device }];
    const o = json(payload), serial = text(o?.serial);
    if (!o || !serial) return [];
    const name = text(o.name), user = text(o.used_by);
    return [
      { fact: FACT.device, device, kind: REMOTE_KIND.known, name: name ?? serial, settings: { [REMOTE_NAME]: name ?? "" },
        // utilisee par un volet : ni suppression ni oubli (le boitier refuserait)
        hidden: user ? [REMOTE_CONTROL.remove, REMOTE_CONTROL.ignore] : [REMOTE_CONTROL.usedBy] },
      ...(user ? [{ fact: FACT.state, device, control: REMOTE_CONTROL.usedBy, value: user } satisfies Fact] : []),
    ];
  }
  m = /^ignored\/([^/]+)$/.exec(path);
  if (m) {
    const device = IGNORED_PREFIX + m[1]!;
    if (payload.trim() === "") return [{ fact: FACT.gone, device }];
    const serial = text(json(payload)?.serial);
    return serial ? [{ fact: FACT.device, device, kind: REMOTE_KIND.ignored, name: serial }] : [];
  }
  return null;
}

/** Bouton d'une carte de telecommande -> commande ; null si ce n'en est pas un. */
export function remoteCommand(device: string, control: string): FamilyCommand | null {
  if (control === REMOTE_CONTROL.remove) return { path: `remote/${device}/set`, payload: "DELETE" };
  if (control === REMOTE_CONTROL.ignore) return { path: `remote/${device}/set`, payload: "IGNORE" };
  if (control === REMOTE_CONTROL.accept && device.startsWith(IGNORED_PREFIX)) return { path: `ignored/${device.slice(IGNORED_PREFIX.length)}/set`, payload: "ACCEPT" };
  return null;
}

/** Renommer une telecommande -> remote/<slug>/config. */
export const remoteConfigure = (device: string, name: string): FamilyCommand => ({ path: `remote/${device}/config`, payload: JSON.stringify({ name }) });
