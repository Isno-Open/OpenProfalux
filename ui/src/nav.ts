// Les onglets et leurs sections. Les identifiants sont ceux des adresses d'aujourd'hui (#control,
// #remotes/add, #sys/log...) : favoris, liens de la documentation et portail captif restent valides.
// Wi-Fi, MQTT et Cloud deviennent les sections de Reseau.
import { SECTION, TAB, type SectionId, type TabId } from './domain';
import { ICON, type IconName } from './icons';

export interface Section {
  id: SectionId;
  label: string;
  hidden?: boolean;          // un parcours d'ajout : pas de sous-onglet a lui
  parent?: SectionId;        // le sous-onglet qu'il allume
}
export interface Tab {
  id: TabId;
  label: string;
  short?: string;            // libelle de la barre du bas, sur telephone, si le libelle entier n'y tient pas
  icon: IconName;
  sections?: Section[];
  badge?: string;
}

export const TABS: Tab[] = [
  { id: TAB.control, label: 'Volets', icon: ICON.shutter },
  {
    id: TAB.remotes, label: 'Appareils', icon: ICON.devices,
    sections: [
      { id: SECTION.add, label: 'Ajouter un appareil' },
      { id: SECTION.devices, label: 'Mes appareils' },
      { id: SECTION.central, label: 'Centrale' },
      { id: SECTION.calibration, label: 'Calibration' },
      { id: SECTION.captured, label: 'Captées' },
      // Les parcours d'ajout : pas de sous-onglet a eux, ils allument « Ajouter un appareil ».
      { id: SECTION.learn, label: 'Capturer', hidden: true, parent: SECTION.add },
      { id: SECTION.replay, label: 'Capturer', hidden: true, parent: SECTION.add },
      { id: SECTION.enrol, label: 'Télécommande virtuelle', hidden: true, parent: SECTION.add },
      { id: SECTION.somfy, label: 'Somfy RTS', hidden: true, parent: SECTION.add },
      { id: SECTION.io, label: 'io-homecontrol', hidden: true, parent: SECTION.add },
    ],
  },
  {
    id: TAB.network, label: 'Réseau', icon: ICON.network,
    sections: [{ id: SECTION.wifi, label: 'Wi-Fi' }, { id: SECTION.mqtt, label: 'MQTT' }, { id: SECTION.cloud, label: 'Cloud' }],
  },
  { id: TAB.ota, label: 'OTA', icon: ICON.ota },
  {
    id: TAB.system, label: 'Système', icon: ICON.system,
    sections: [
      { id: SECTION.device, label: 'Appareil' },
      { id: SECTION.health, label: 'État' },
      { id: SECTION.radio, label: 'Radio' },
      { id: SECTION.backup, label: 'Sauvegarde' },
      { id: SECTION.rf, label: 'RF (debug)' },
      { id: SECTION.log, label: 'Journal' },
    ],
  },
  { id: TAB.support, label: 'Support', icon: ICON.support },
  { id: TAB.about, label: 'À propos', icon: ICON.info },
];
