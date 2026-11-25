// Identifiants des controles de la famille OpenProfalux : declares ici une fois, utilises par le
// generateur des types (scripts/gen-kinds.ts) et par la traduction des commandes (index.ts).
// step : un appui du bouton pas a pas d'un portail (profiles.json, comportement « step », #134).
export const CONTROL_ID = { up: "up", stop: "stop", down: "down", step: "step", position: "position" } as const;
export type ActionId = typeof CONTROL_ID.up | typeof CONTROL_ID.stop | typeof CONTROL_ID.down | typeof CONTROL_ID.step;
export const ACTION_IDS: readonly ActionId[] = [CONTROL_ID.up, CONTROL_ID.stop, CONTROL_ID.down, CONTROL_ID.step];
/** Reglages : cles des champs, et ce que le firmware publie et attend (cover_attrs.c, shutters.c). */
export const SETTING_ID = { travelUp: "travel_up_s", travelDown: "travel_down_s", rank: "rank", orientation: "orientation", members: "members" } as const;
/** Role d'un appareil tel que le firmware le publie (attrs "role", cles de profiles.json "roles") ; un
 *  appareil sans role est simple. Une centrale ou une zone a son propre type d'appareil dans le portail,
 *  « <type>-<role> » (ex. volet-central) : sa carte n'a pas de position, ses reglages ont ses membres. */
export const ROLE_SIMPLE = "simple";
export const roleKindId = (type: string, role: string): string => `${type}-${role}`;
/** Orientation d'une facade : azimut en degres, 0 (nord) a 359 (cover_attrs.h COVER_ORIENTATION_MAX). */
export const ORIENTATION_MAX = 359;
