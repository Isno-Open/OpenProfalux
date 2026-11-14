// Identifiants des controles de la famille OpenProfalux : declares ici une fois, utilises par le
// generateur des types (scripts/gen-kinds.ts) et par la traduction des commandes (index.ts).
export const CONTROL_ID = { up: "up", stop: "stop", down: "down", position: "position" } as const;
export type ActionId = typeof CONTROL_ID.up | typeof CONTROL_ID.stop | typeof CONTROL_ID.down;
export const ACTION_IDS: readonly ActionId[] = [CONTROL_ID.up, CONTROL_ID.stop, CONTROL_ID.down];
