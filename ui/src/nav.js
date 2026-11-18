// Les onglets et leurs sections. Les identifiants sont ceux des adresses d'aujourd'hui (#control,
// #remotes/add, #sys/log...) : favoris, liens de la documentation et portail captif restent valides.
// Wi-Fi, MQTT et Cloud deviennent les sections de Reseau.
export const ONGLETS = [
  { id: 'control', label: 'Volets', icon: 'volet' },
  {
    id: 'remotes', label: 'Appareils', icon: 'appareils',
    sections: [
      { id: 'add', label: 'Ajouter un appareil' },
      { id: 'devices', label: 'Mes appareils' },
      { id: 'central', label: 'Centrale' },
      { id: 'calib', label: 'Calibration' },
      { id: 'list', label: 'Captées' },
      // Les parcours d'ajout : pas de sous-onglet a eux, ils allument « Ajouter un appareil ».
      { id: 'learn', label: 'Capturer', cache: true, parent: 'add' },
      { id: 'rejeu', label: 'Capturer', cache: true, parent: 'add' },
      { id: 'enrol', label: 'Télécommande virtuelle', cache: true, parent: 'add' },
      { id: 'somfy', label: 'Somfy RTS', cache: true, parent: 'add' },
    ],
  },
  { id: 'reseau', label: 'Réseau', icon: 'reseau', sections: [{ id: 'wifi', label: 'Wi-Fi' }, { id: 'mqtt', label: 'MQTT' }, { id: 'cloud', label: 'Cloud' }] },
  { id: 'ota', label: 'OTA', icon: 'ota' },
  {
    id: 'sys', label: 'Système', icon: 'sys',
    sections: [
      { id: 'device', label: 'Appareil' },
      { id: 'health', label: 'État' },
      { id: 'radio', label: 'Radio' },
      { id: 'backup', label: 'Sauvegarde' },
      { id: 'rf', label: 'RF (debug)' },
      { id: 'log', label: 'Journal' },
    ],
  },
  { id: 'support', label: 'Support', icon: 'support' },
  { id: 'about', label: 'À propos', icon: 'info' },
];
