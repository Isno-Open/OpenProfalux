// Les icones du jeu dessine pour le projet (assets/icons.svg.txt, symboles « i-<nom> »).
export const ICON = {
  shutter: 'shutter', remote: 'remote', network: 'network', ota: 'ota', system: 'system', info: 'info', central: 'central',
  battery: 'battery', batteryLow: 'battery-low', zone: 'zone', gate: 'gate', devices: 'devices', support: 'support', cloud: 'cloud',
} as const;
export type IconName = (typeof ICON)[keyof typeof ICON];
