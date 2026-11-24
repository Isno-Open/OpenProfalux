// Le profil d'une fiche d'appareil (issue #134) : la regle que le firmware applique avant chaque enregistrement
// (profile_table.c, profile_for_device), ecrite une fois cote interface. Module PUR, sans le store : l'interface
// s'en sert pour une fiche d'un firmware anterieur (sans profil grave), et le mode demo pour se comporter comme le
// boitier quand la marque d'un appareil change.
import type { Device, Profile } from '../api/types';
import { ADD_METHOD, BRAND, ENGINE, PROTOCOL, type AddMethod, type Engine } from '../domain';

export const isSomfy = (device: Pick<Device, 'proto'> | null | undefined): boolean => device?.proto === PROTOCOL.somfy;

/* Les deux axes d'un appareil, deduits de sa fiche : enrole (virtuel, Somfy ou KeeLoq), rejeu brut, ou clone. */
export interface Method { add: AddMethod; engine: Engine }
export function methodOf(device: Device): Method {
  if (device.virt) return { add: ADD_METHOD.enrol, engine: isSomfy(device) ? ENGINE.somfy : ENGINE.keeloq };
  return { add: ADD_METHOD.copy, engine: device.rejeu ? ENGINE.replay : ENGINE.keeloq };
}
export const sameMethod = (p: Profile, m: Method): boolean => p.add === m.add && p.engine === m.engine;

/* Marque d'un appareil : celle gravee dans sa fiche, sinon deduite du protocole. */
export const brandOf = (device: Device): string => device.marque || (isSomfy(device) ? BRAND.somfy : device.rejeu ? BRAND.came : BRAND.profalux);

/* Le profil a garder pour une fiche : celui qu'elle porte s'il reste coherent (meme methode, et meme marque quand
   la fiche en porte une), sinon celui de sa marque et de sa methode, sinon le premier de sa methode. */
export function profileForDevice(profiles: readonly Profile[], device: Device): Profile | null {
  const method = methodOf(device);
  const own = device.profile ? profiles.find(p => p.id === device.profile) : undefined;
  if (own && sameMethod(own, method) && (!device.marque || own.brand === device.marque)) return own;
  const brand = brandOf(device);
  const ofMethod = profiles.filter(p => sameMethod(p, method));
  return ofMethod.find(p => p.brand === brand) ?? ofMethod[0] ?? null;
}
