// Les valeurs du domaine, declarees une seule fois (AGENTS.md : pas de texte compare a la main). Le code
// s'y refere par leur nom. Les valeurs qui partent sur le fil (firmware, profiles.json, adresses) gardent la
// forme que le firmware attend : seul le nom, cote interface, est en anglais.

type ValueOf<T> = T[keyof T];

/* Commandes d'un appareil (corps de /api/shutter, /api/pfx/cmd, captures). */
export const COMMAND = { up: 'up', stop: 'stop', down: 'down', position: 'pos', prog: 'prog' } as const;
export type Command = ValueOf<typeof COMMAND>;
/* Les trois commandes d'un pave, dans l'ordre d'affichage. */
export const MOVE_COMMANDS = [COMMAND.up, COMMAND.stop, COMMAND.down] as const;
export type MoveCommand = (typeof MOVE_COMMANDS)[number];
const COMMAND_VALUES: readonly string[] = Object.values(COMMAND);
/* Une commande lue dans profiles.json (types[].capture) : verifiee avant d'etre envoyee. */
export const isCommand = (value: string): value is Command => COMMAND_VALUES.includes(value);
/* Serial vide d'une telecommande (aucune trame, aucune identite) dans les reponses du firmware. */
export const NO_SERIAL = '0x0000000';
/* Choix « nouvel appareil » d'une liste de puces (valeur interne, jamais un nom d'appareil valide). */
export const NEW_DEVICE = '__new';

/* Types d'appareil : cles de profiles.json « types », et leur code dans la fiche (champ type). */
export const DEVICE_TYPE = { shutter: 'volet', gate: 'portail' } as const;
export type DeviceType = ValueOf<typeof DEVICE_TYPE>;
export const DEVICE_TYPE_CODE = { shutter: 0, gate: 1 } as const;

/* Protocole d'un appareil virtuel (champ proto). */
export const PROTOCOL = { somfy: 'somfy', profalux: 'profalux' } as const;
export type Protocol = ValueOf<typeof PROTOCOL>;

/* Mecanisme d'un profil (profiles.json « mecanisme ») et d'un appareil. */
export const MECHANISM = { enrolment: 'enrolement', replay: 'rejeu', clone: 'cloner' } as const;
export type Mechanism = ValueOf<typeof MECHANISM>;

/* Parcours d'ajout d'un profil (profiles.json « flow »). */
export const FLOW = { learn: 'learn', replay: 'rejeu', enrolPfx: 'enrol-pfx', createSomfy: 'create-somfy' } as const;
export type Flow = ValueOf<typeof FLOW>;

/* Marques nommees par le code (profiles.json « marque »). */
export const BRAND = { profalux: 'Profalux', somfy: 'Somfy RTS', came: 'CAME' } as const;

/* Role d'un appareil (profiles.json « roles », membres permis d'une centrale ou d'une zone). */
export const ROLE = { simple: 'simple', central: 'central', zone: 'zone' } as const;
export type Role = ValueOf<typeof ROLE>;

/* Bandes radio (champ band des radios et des profils). */
export const BAND = { mhz868: '868', mhz433: '433' } as const;

/* Profils de reception d'une radio (champ radio_profile). */
export const RADIO_PROFILE = { profalux: 'profalux', eveno: 'eveno', custom: 'custom' } as const;

/* Etats de /api/ota/status (champ state) et de l'image (champ img). */
export const OTA_STATE = { idle: 0, rebooting: 4, failed: 99 } as const;
/* Instant du dernier demarrage du boitier (ms, horloge du navigateur), d'apres son uptime (s, /api/status) lu a
   l'instant at : il avance a chaque redemarrage. L'uptime est arrondi a la seconde : deux demarrages se
   distinguent au-dela de REBOOT_MARGIN_MS. */
export const bootInstant = (uptimeS: number, at: number): number => at - uptimeS * 1000;
export const REBOOT_MARGIN_MS = 3000;
export const OTA_IMAGE = { trial: 'essai' } as const;

/* Balayage « Caler sur ma telecommande » (/api/rx/sweep) et calibration du gain (/api/rx/calibrate). */
export const SCAN_STATE = { running: 1, done: 2 } as const;
export const SWEEP_STATUS = { found: 0, tooWeak: -2, outOfRange: -3 } as const;

/* Appairage cloud par QR (/api/cloud/grant, champ state). */
export const GRANT_STATE = { pending: 'pending', ready: 'ready', expired: 'expired', denied: 'denied', error: 'error' } as const;
/* Refus d'une demande d'appairage : le boitier est deja appaire (champ err). */
export const CLOUD_ALREADY_PAIRED = 'paired';

/* Raisons de refus internes a l'interface (rejets de la couche api). */
export const FAILURE = {
  nothing: 'nothing',          // rien capte
  busy: 'busy',                // radio deja prise
  timeout: 'timeout',          // delai d'attente depasse
  silent: 'silent',            // le boitier ne repond pas
  github: 'github',            // verification GitHub finie sans resultat
  start: 'start',              // l'operation n'a pas demarre
  refused: 'refused',          // refus metier sans raison
} as const;
export type Failure = ValueOf<typeof FAILURE>;
export const HTTP_UNAUTHORIZED = 401;
export const HTTP_CONFLICT = 409;

/* Ton d'un message ou d'un badge (classe CSS du composant). */
export const TONE = { ok: 'ok', warning: 'warning', danger: 'danger', neutral: 'neutral', accent: 'accent' } as const;
export type Tone = ValueOf<typeof TONE>;

/* Variante d'un bouton (classe CSS du composant). */
export const BUTTON = { normal: 'normal', primary: 'primary', ghost: 'ghost', danger: 'danger' } as const;
export type ButtonVariant = ValueOf<typeof BUTTON>;

/* Onglets et sections : les ancres d'adresse d'aujourd'hui (#control, #remotes/add, #sys/log...), qui sont des
   donnees (favoris, liens de la documentation, portail captif /#wifi). */
export const TAB = {
  control: 'control', remotes: 'remotes', network: 'reseau', ota: 'ota', system: 'sys', support: 'support', about: 'about',
} as const;
export type TabId = ValueOf<typeof TAB>;
export const SECTION = {
  add: 'add', devices: 'devices', central: 'central', calibration: 'calib', captured: 'list',
  learn: 'learn', replay: 'rejeu', enrol: 'enrol', somfy: 'somfy',
  wifi: 'wifi', mqtt: 'mqtt', cloud: 'cloud',
  device: 'device', health: 'health', radio: 'radio', backup: 'backup', rf: 'rf', log: 'log',
} as const;
export type SectionId = ValueOf<typeof SECTION>;
/* Suite d'adresse qui ouvre la capture sur un appareil existant (#remotes/learn/-/<nom>). */
export const EXISTING_DEVICE = '-';

/* Champs d'emission reglables (cles de /api/volet/txparams). */
export const TX_FIELD = { frequency: 'freq_khz', te: 'te_us', repeat: 'tx_rpt', rmt: 'tx_rmt', frameBits: 'frame_bits' } as const;
export type TxFieldName = ValueOf<typeof TX_FIELD>;
/* Genre d'un champ d'emission, pour le controle qui le saisit. */
export const FIELD_KIND = { number: 'number', toggle: 'toggle', choice: 'choice' } as const;
export type FieldKind = ValueOf<typeof FIELD_KIND>;
/* Drapeau oui / non que le firmware envoie en nombre (« %d » : 0 ou 1, /api/config, /api/ota/status...). Il
   accepte un booleen en ecriture. */
export const FLAG = { off: 0, on: 1 } as const;
export type Flag = ValueOf<typeof FLAG>;
/* Lecture d'un drapeau : absent (firmware d'avant) = la valeur par defaut. */
export const isFlagOn = (flag: Flag | undefined, absent: boolean): boolean => (flag === undefined ? absent : flag !== FLAG.off);
/* Tri-etat d'un reglage oui / non d'une fiche (tx_rpt, tx_rmt) : 0 = celui du profil. */
export const TRISTATE = { inherit: 0, no: 1, yes: 2 } as const;

/* Fichiers telecharges (liens directs du boitier). */
export const DOWNLOAD = { backup: 'backup', frames: 'frames', log: 'log' } as const;
export type DownloadKey = ValueOf<typeof DOWNLOAD>;

/* Phases de la verification OTA, pour le message d'attente. */
export const CHECK_PHASE = { waiting: 'waiting', github: 'github' } as const;
export type CheckPhase = ValueOf<typeof CHECK_PHASE>;

/* Sources d'une mise a jour, et confirmations de l'ecran OTA. */
export const OTA_SOURCE = { github: 'github', file: 'file' } as const;
export type OtaSource = ValueOf<typeof OTA_SOURCE>;
export const OTA_CONFIRM = { none: '', github: 'github', file: 'file', rollback: 'rollback' } as const;
export type OtaConfirm = ValueOf<typeof OTA_CONFIRM>;

/* Theme de l'interface (attribut data-theme, cle opfx_theme du navigateur). */
export const THEME = { dark: 'dark', light: 'light' } as const;
export type Theme = ValueOf<typeof THEME>;
export const THEME_STORAGE_KEY = 'opfx_theme';

/* But d'un rapport Support (champ purpose de /api/report). */
export const REPORT_PURPOSE = { debug: 'debug', newBrand: 'new-brand' } as const;

