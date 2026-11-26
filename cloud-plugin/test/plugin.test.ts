// Tests du plugin OpenProfalux : ils REJOUENT les messages reels d'un boitier (M5 ATOM Lite,
// opf-2d5efba03f, firmware 0.2.16, releves sur le broker cloud le 2026-11-14), puis passent la famille
// au vrai registre d'OpenCloudApp, qui la verifie comme n'importe quel plugin venu d'ailleurs.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import plugin, { family, read, command, configure, ACTION_PAYLOAD } from "../src/index.js";
import { kindsOf, iconOf } from "../scripts/gen-kinds.js";
import { FACT, DEVICE_SETTING } from "../opencloudapp/server/src/family-contract.js";
import { registerDeviceFamily, clearDeviceFamilies, applyMessage, boxState, buildCommand, buildConfigure, familyById, buildFileRequest, awaitBoxFile } from "../opencloudapp/server/src/device-families.js";

const ROOT = "openprofalux";
const BOX = "opf-2d5efba03f";
// releve reel : mosquitto_sub -t "openprofalux/opf-2d5efba03f/#" -v (identite relais), 2026-11-14
const REAL: readonly [string, string][] = [
  ["status", "online"],
  ["openprofalux/state", '{"last_cmd":"HEARTBEAT","rssi":-70,"free_heap":31808,"min_heap":19424,"tls_pool":23552,"tls_free":16148,"tls_min":1864,"tls_spills":0}'],
  ["cover/Chambre_Parents/attrs", '{"type":"volet","positional":true}'],
  ["cover/Chambre_Parents/position", "0"],
  ["cover/Chambre_Parents/state", "closed"],
  ["system/status", '{"fw":"0.2.16-stacks3test","target":"m5-atom-lite","free_heap":37904}'],
  ["listen/state", "ON"],
  ["update/state", '{"installed_version":"0.2.16-stacks3test","latest_version":"0.2.15","title":"OpenProfalux","release_url":"https://github.com/Isno-Open/OpenProfalux/releases"}'],
  ["remote/0000813/battery", "OFF"],
];
const here = dirname(fileURLToPath(import.meta.url));
const profiles = (): unknown => JSON.parse(readFileSync(join(here, "../../../firmware/main/web/profiles.json"), "utf8"));

test("la famille passe la verification du portail, et le plugin la declare", () => {
  clearDeviceFamilies();
  let declared: unknown = null;
  void plugin.register({ registerDeviceFamily: (f: unknown) => { declared = f; } } as never);
  assert.equal(declared, family);
  registerDeviceFamily(family);
  assert.equal(familyById("openprofalux")?.topicRoot, ROOT);
});

test("le releve reel d'un boitier donne : en ligne, un volet positionnel ferme (100 % de fermeture), sa version", () => {
  clearDeviceFamilies();
  registerDeviceFamily(family);
  for (const [path, payload] of REAL) {
    const r = applyMessage(`${ROOT}/${BOX}/${path}`, payload, true, 1)!;
    assert.deepEqual(r.refused, [], `rien de refuse pour ${path}`);
  }
  const s = boxState("openprofalux", BOX)!;
  assert.equal(s.online, true);
  assert.deepEqual([s.firmware, s.latest], ["0.2.16-stacks3test", "0.2.15"]);
  const d = s.devices.get("Chambre_Parents")!;
  assert.deepEqual([d.kind, d.known, d.name, [...d.hidden], d.values.get("position")], ["volet", true, "Chambre Parents", [], 100]);
  // publie 0 (% d'ouverture HA) avec l'etat "closed" : le portail montre 100 % comme l'interface du boitier
  // (0 % = ouvert), constate sur le vrai volet ferme le 2026-11-15
  assert.equal(s.devices.size, 1, "battement, ecoute radio et etat texte ne creent pas d'appareil");
});

test("nom : celui que le boitier publie (accents compris), sinon le slug rendu lisible", () => {
  const nom = (attrs: string): string | undefined => {
    const f = read({ box: BOX, path: "cover/Sejour_d_ete/attrs", payload: attrs, retained: true })[0];
    return f && f.fact === FACT.device ? f.name : undefined;
  };
  assert.equal(nom('{"type":"volet","positional":true,"name":"Séjour d\'été"}'), "Séjour d'été");
  assert.equal(nom('{"type":"volet","positional":true}'), "Sejour d ete", "firmware plus ancien : le slug, espaces rendus");
  assert.equal(nom('{"type":"volet","positional":true,"name":"  "}'), "Sejour d ete", "nom vide : le slug");
});

test("commandes : celles que le firmware attend, sous le prefixe du boitier", () => {
  clearDeviceFamilies();
  registerDeviceFamily(family);
  applyMessage(`${ROOT}/${BOX}/cover/Chambre_Parents/attrs`, '{"type":"volet","positional":true}', true, 1);
  const f = familyById("openprofalux")!;
  const target = { box: BOX, device: "Chambre_Parents" };
  // le STOP a ete verifie sur le vrai M5 : il arrive sur openprofalux/cover/Chambre_Parents/set
  assert.deepEqual(buildCommand(f, target, "stop", null), { topic: `${ROOT}/${BOX}/cover/Chambre_Parents/set`, payload: "STOP", retain: false });
  assert.deepEqual(buildCommand(f, target, "up", null), { topic: `${ROOT}/${BOX}/cover/Chambre_Parents/set`, payload: "OPEN", retain: false });
  assert.deepEqual(buildCommand(f, target, "position", 42), { topic: `${ROOT}/${BOX}/cover/Chambre_Parents/set_position`, payload: "58", retain: false },
    "42 % de fermeture = 58 % d'ouverture, ce qu'attend le firmware");
  // la mise a jour du boitier entier : voir le test « mises a jour » (seulement quand elle est possible)
  assert.deepEqual(Object.keys(ACTION_PAYLOAD).sort(), ["down", "stop", "up"]);
});

test("renommage sur le boitier : le meme appareil, sous son nouveau nom, et les commandes suivent", () => {
  clearDeviceFamilies();
  registerDeviceFamily(family);
  const devices = (): unknown[] => [...boxState("openprofalux", BOX)!.devices].map(([slug, d]) => [slug, d.uid, d.name]);
  // ce que publie le firmware : l'identifiant stable, pris au slug du moment a la creation
  applyMessage(`${ROOT}/${BOX}/cover/Volet_Test/attrs`, '{"type":"volet","positional":true,"uid":"Volet_Test","name":"Volet Test"}', true, 1);
  assert.deepEqual(devices(), [["Volet_Test", "Volet_Test", "Volet Test"]]);
  // renomme : le firmware retire l'ancienne description (vide) puis publie la nouvelle, MEME identifiant
  applyMessage(`${ROOT}/${BOX}/cover/Volet_Test/attrs`, "", true, 2);
  applyMessage(`${ROOT}/${BOX}/cover/Test_Google/attrs`, '{"type":"volet","positional":true,"uid":"Volet_Test","name":"Test Google"}', true, 2);
  assert.deepEqual(devices(), [["Test_Google", "Volet_Test", "Test Google"]], "un seul appareil, la meme identite, le nouveau nom");
  assert.deepEqual(buildCommand(familyById("openprofalux")!, { box: BOX, device: "Test_Google" }, "up", null),
    { topic: `${ROOT}/${BOX}/cover/Test_Google/set`, payload: "OPEN", retain: false }, "la commande vise le nouveau nom, celui que le boitier ecoute");
  // un firmware d'avant n'annonce pas d'identifiant : rien n'est invente
  assert.deepEqual(read({ box: BOX, path: "cover/Ancien/attrs", payload: '{"type":"volet","positional":true}', retained: true })[0], { fact: FACT.device, device: "Ancien", kind: "volet", name: "Ancien", hidden: [], settings: {} });
});

test("un portail (non positionnel) n'a ni position affichee ni commande de position", () => {
  clearDeviceFamilies();
  registerDeviceFamily(family);
  applyMessage(`${ROOT}/${BOX}/cover/Portail/attrs`, '{"type":"portail","positional":false}', true, 1);
  const f = familyById("openprofalux")!;
  assert.ok("error" in buildCommand(f, { box: BOX, device: "Portail" }, "position", 10));
  assert.equal(buildCommand(f, { box: BOX, device: "Portail" }, "down", null) && "topic" in buildCommand(f, { box: BOX, device: "Portail" }, "down", null), true);
});

test("pile faible : une alerte quand elle est vue en direct, pas au branchement (message retenu)", () => {
  assert.deepEqual(read({ box: BOX, path: "remote/0000813/battery", payload: "ON", retained: true }), []);
  const ev = read({ box: BOX, path: "remote/0000813/battery", payload: "ON", retained: false });
  assert.equal(ev.length, 1);
  assert.equal(ev[0]!.fact, FACT.event);
  assert.deepEqual(read({ box: BOX, path: "remote/0000813/battery", payload: "OFF", retained: false }), []);
});

test("messages abimes : rien plutot qu'un fait faux", () => {
  assert.deepEqual(read({ box: BOX, path: "cover/x/attrs", payload: "pas du json", retained: true }), []);
  assert.deepEqual(read({ box: BOX, path: "cover/x/position", payload: "", retained: true }), []);
  assert.deepEqual(read({ box: BOX, path: "status", payload: "peut-etre", retained: true }), []);
  assert.equal(command({ box: BOX, device: "x" }, "voler", null), null);
});

test("types engendres depuis profiles.json : volet positionnel, portail sans position, icones sans balisage", () => {
  const kinds = kindsOf(profiles()) as { id: string; icon: Record<string, string>; controls: { id: string }[] }[];
  const volet = kinds.find((k) => k.id === "volet")!, portail = kinds.find((k) => k.id === "portail")!;
  assert.ok(volet.controls.some((c) => c.id === "position"));
  assert.ok(!portail.controls.some((c) => c.id === "position"));
  assert.equal(typeof portail.icon.path, "string", "l'icone SVG du firmware devient un trace");
  assert.deepEqual(family.kinds.map((k) => k.id), [...kinds.map((k) => k.id), "remote", "remote-ignored"], "le plugin porte les types du firmware, puis ses telecommandes");
});

test("types engendres : un type sans libelles anglais, ou une icone SVG a plusieurs traces, sont refuses", () => {
  const p = profiles() as { types: Record<string, Record<string, unknown>> };
  const sansEn = structuredClone(p);
  delete sansEn.types.volet!.en;
  assert.throws(() => kindsOf(sansEn), /anglais/);
  assert.throws(() => iconOf("x", "<svg><path d='M0 0'/><path d='M1 1'/></svg>"), /un seul/);
});

test("le moteur de plugins est a la meme version que celle d'OpenCloudApp (une seule version)", () => {
  const dep = (p: string): string => (JSON.parse(readFileSync(join(here, p), "utf8")) as { dependencies: Record<string, string> }).dependencies["@isno-open/plugin-host"]!;
  assert.equal(dep("../../package.json"), dep("../../opencloudapp/server/package.json"));
});

test("etalonnage : temps de course lus dans la description, envoyes en millisecondes sur config", () => {
  clearDeviceFamilies();
  registerDeviceFamily(family);
  applyMessage(`${ROOT}/${BOX}/cover/Chambre_Parents/attrs`, '{"type":"volet","positional":true,"name":"Chambre Parents","travel_up_s":19,"travel_down_s":18}', true, 1);
  const d = boxState("openprofalux", BOX)!.devices.get("Chambre_Parents")!;
  assert.deepEqual([d.settings.get("travel_up_s"), d.settings.get("travel_down_s")], [19, 18]);
  const f = familyById("openprofalux")!;
  assert.deepEqual(buildConfigure(f, { box: BOX, device: "Chambre_Parents" }, { travel_up_s: 19, travel_down_s: 17 }),
    { topic: `${ROOT}/${BOX}/cover/Chambre_Parents/config`, payload: '{"travel_up_ms":19000,"travel_down_ms":17000}', retain: false });
  assert.ok("error" in buildConfigure(f, { box: BOX, device: "Chambre_Parents" }, { travel_up_s: 0, travel_down_s: 17 }), "sous la borne de profiles.json");
  assert.ok("error" in buildConfigure(f, { box: BOX, device: "Chambre_Parents" }, { travel_up_s: 19, travel_down_s: 181 }), "au-dessus");
  assert.equal(configure({ box: BOX, device: "x" }, { travel_up_s: 19 }), null, "un seul des deux : rien n'est envoye");
  assert.deepEqual(configure({ box: BOX, device: "x" }, { travel_up_s: 19, rank: 2 }), { path: "cover/x/config", payload: '{"rank":2}' }, "temps de course seul ignore, le rang passe");
});

test("etalonnage : un volet non etalonne n'a pas de valeurs ; un portail n'a pas de reglages", () => {
  clearDeviceFamilies();
  registerDeviceFamily(family);
  applyMessage(`${ROOT}/${BOX}/cover/Chambre_Parents/attrs`, '{"type":"volet","positional":false,"name":"Chambre Parents"}', true, 1);
  assert.equal(boxState("openprofalux", BOX)!.devices.get("Chambre_Parents")!.settings.size, 0);
  const portail = family.kinds.find((k) => k.id === "portail")!;
  assert.deepEqual(portail.settings!.map((x) => x.key), ["rank"], "un portail : seulement son rang");
  const volet = family.kinds.find((k) => k.id === "volet")!;
  assert.deepEqual(volet.settings!.map((x) => [x.key, "min" in x ? x.min : null, "max" in x ? x.max : null]),
    [["rank", 1, 24], ["orientation", 0, 359], ["travel_up_s", 1, 180], ["travel_down_s", 1, 180]], "bornes de profiles.json");
});

test("sante : battement et etat systeme reels du M5 (signal, memoire, reseau, redemarrage)", () => {
  clearDeviceFamilies();
  registerDeviceFamily(family);
  // releve reel apres la mise a jour 0.2.16-santetest, 2026-11-14
  applyMessage(`${ROOT}/${BOX}/system/status`, '{"fw":"0.2.16-santetest","target":"m5-atom-lite","free_heap":46128,"ssid":"Isno","ip":"192.168.20.12","mqtt":"192.168.50.7:1883","reset":"software"}', true, 1);
  applyMessage(`${ROOT}/${BOX}/openprofalux/state`, '{"last_cmd":"HEARTBEAT","rssi":-67,"free_heap":33324,"min_heap":21820,"tls_pool":23552,"up_s":9908}', true, 1);
  const s = boxState("openprofalux", BOX)!;
  assert.deepEqual(s.network, { ssid: "Isno", ip: "192.168.20.12", broker: "192.168.50.7:1883" });
  assert.deepEqual([s.health.rssiDbm, s.health.freeHeap, s.health.minFreeHeap, s.health.uptimeS], [-67, 33324, 21820, 9908]);
  assert.match(s.health.lastReset!.fr, /logiciel/);
  assert.ok(read({ box: BOX, path: "listen/state", payload: "ON", retained: true }).every((x) => x.fact !== FACT.box || !("health" in x)), "l'ecoute radio n'est pas un battement");
  assert.deepEqual(read({ box: BOX, path: "openprofalux/state", payload: '{"last_cmd":"X"}', retained: true }), [], "sans signal, ce n'est pas un battement");
});

test("mises a jour : installer seulement vers plus recent ; retour arriere quand le boitier le permet", () => {
  clearDeviceFamilies();
  registerDeviceFamily(family);
  const f = familyById("openprofalux")!;
  // releve reel : version de test plus recente que la publiee -> pas d'installation proposee
  applyMessage(`${ROOT}/${BOX}/update/state`, '{"installed_version":"0.2.16-otatest","latest_version":"0.2.15"}', true, 1);
  applyMessage(`${ROOT}/${BOX}/system/ota`, '{"trial":false,"previous":"0.2.16-santetest","rollback":true}', true, 1);
  const s = boxState("openprofalux", BOX)!;
  assert.deepEqual([s.actions.get("update"), s.actions.get("rollback"), s.trial, s.previous], [false, true, false, "0.2.16-santetest"]);
  assert.ok("error" in buildCommand(f, { box: BOX }, "update", null), "rien a installer : refuse");
  assert.deepEqual(buildCommand(f, { box: BOX }, "rollback", null), { topic: `${ROOT}/${BOX}/ota/rollback`, payload: "ROLLBACK", retain: false });
  applyMessage(`${ROOT}/${BOX}/update/state`, '{"installed_version":"0.2.15","latest_version":"0.2.16"}', true, 1);
  assert.deepEqual(buildCommand(f, { box: BOX }, "update", null), { topic: `${ROOT}/${BOX}/update/install`, payload: "INSTALL", retain: false });
  applyMessage(`${ROOT}/${BOX}/system/ota`, '{"trial":true,"previous":"","rollback":false}', true, 1);
  assert.ok("error" in buildCommand(f, { box: BOX }, "rollback", null), "pas de version precedente valide : refuse");
  assert.equal(boxState("openprofalux", BOX)!.previous, null);
});

test("ecoute permanente : etat reel lu sur listen/state, seule l'action qui le change est possible", () => {
  clearDeviceFamilies();
  registerDeviceFamily(family);
  const f = familyById("openprofalux")!;
  // releve reel du M5 : ecoute active -> on peut la couper, pas l'activer
  applyMessage(`${ROOT}/${BOX}/listen/state`, "ON", true, 1);
  let s = boxState("openprofalux", BOX)!;
  assert.deepEqual([s.actions.get("listen-on"), s.actions.get("listen-off")], [false, true]);
  assert.deepEqual(buildCommand(f, { box: BOX }, "listen-off", null), { topic: `${ROOT}/${BOX}/listen/set`, payload: "OFF", retain: false });
  assert.ok("error" in buildCommand(f, { box: BOX }, "listen-on", null), "deja active : refuse");
  applyMessage(`${ROOT}/${BOX}/listen/state`, "OFF", true, 1);
  s = boxState("openprofalux", BOX)!;
  assert.deepEqual([s.actions.get("listen-on"), s.actions.get("listen-off")], [true, false]);
  assert.deepEqual(buildCommand(f, { box: BOX }, "listen-on", null), { topic: `${ROOT}/${BOX}/listen/set`, payload: "ON", retain: false });
  // un mot inconnu ne change rien (pas d'etat invente)
  assert.deepEqual(read({ box: BOX, path: "listen/state", payload: "peut-etre", retained: true }), []);
  // couper demande confirmation (les positions ne sont plus suivies), activer non
  const ctl = (id: string) => family.boxControls!.find((c) => c.id === id)!;
  assert.ok(ctl("listen-off").confirm && !ctl("listen-on").confirm);
});

test("rang et orientation : lus dans la description reelle, envoyes sur config", () => {
  clearDeviceFamilies();
  registerDeviceFamily(family);
  // releve reel du M5 (0.2.16-ranktest) apres {"orientation":200,"rank":1}
  applyMessage(`${ROOT}/${BOX}/cover/Chambre_Parents/attrs`, '{"type":"volet","positional":true,"name":"Chambre Parents","travel_up_s":19,"travel_down_s":19,"rank":1,"orientation":200}', true, 1);
  const d = boxState("openprofalux", BOX)!.devices.get("Chambre_Parents")!;
  assert.deepEqual([d.rank, d.settings.get("rank"), d.settings.get("orientation")], [1, 1, 200]);
  assert.deepEqual(buildConfigure(familyById("openprofalux")!, { box: BOX, device: "Chambre_Parents" }, { orientation: 270, rank: 1 }),
    { topic: `${ROOT}/${BOX}/cover/Chambre_Parents/config`, payload: '{"rank":1,"orientation":270}', retain: false });
  assert.ok("error" in buildConfigure(familyById("openprofalux")!, { box: BOX, device: "Chambre_Parents" }, { orientation: 360 }), "hors bornes");
});

test("centrale : releve reel (role et membres), carte sans position, membres choisis parmi les appareils du boitier", () => {
  clearDeviceFamilies();
  registerDeviceFamily(family);
  // releve reel du M5 (0.2.16-rolestest), centrale de test creee sur le boitier avec Chambre Parents
  applyMessage(`${ROOT}/${BOX}/cover/Chambre_Parents/attrs`, '{"type":"volet","positional":true,"name":"Chambre Parents","travel_up_s":19,"travel_down_s":19,"rank":1,"orientation":200}', true, 1);
  const r = applyMessage(`${ROOT}/${BOX}/cover/Test_centrale/attrs`, '{"type":"volet","positional":false,"name":"Test centrale","role":"central","members":["Chambre_Parents"]}', true, 1)!;
  assert.deepEqual(r.refused, []);
  const c = boxState("openprofalux", BOX)!.devices.get("Test_centrale")!;
  assert.deepEqual([c.kind, c.known, c.name, c.settings.get("members")], ["volet-central", true, "Test centrale", ["Chambre_Parents"]]);
  const kind = family.kinds.find((k) => k.id === "volet-central")!;
  assert.ok(!kind.controls.some((x) => x.id === "position"), "une centrale n'a pas de position");
  const f = familyById("openprofalux")!, t = { box: BOX, device: "Test_centrale" };
  assert.deepEqual(buildConfigure(f, t, { members: [] }), { topic: `${ROOT}/${BOX}/cover/Test_centrale/config`, payload: '{"members":[]}', retain: false });
  assert.deepEqual(buildConfigure(f, t, { members: ["Chambre_Parents"] }), { topic: `${ROOT}/${BOX}/cover/Test_centrale/config`, payload: '{"members":["Chambre_Parents"]}', retain: false });
  assert.ok("error" in buildConfigure(f, t, { members: ["Salon"] }), "absent du boitier");
  assert.ok("error" in buildConfigure(f, t, { members: ["Test_centrale"] }), "elle-meme");
  // un volet simple n'a pas de membres a regler (devenir centrale se fait sur le boitier)
  assert.ok("error" in buildConfigure(f, { box: BOX, device: "Chambre_Parents" }, { members: [] }));
});

test("zone : ses suiveurs sont des volets simples ; une centrale peut contenir une zone, pas une autre centrale", () => {
  const byId = (id: string) => family.kinds.find((k) => k.id === id)!;
  const pick = (id: string): readonly string[] => {
    const f = byId(id).settings!.find((s) => s.key === "members")!;
    return f.kind === DEVICE_SETTING.devices ? f.kinds : [];
  };
  assert.deepEqual(pick("volet-zone"), ["volet"]);
  assert.ok(pick("volet-central").includes("volet-zone") && !pick("volet-central").some((k) => k.endsWith("-central")));
  assert.ok(!family.kinds.some((k) => k.id === "portail-zone"), "pas de zone de portails (role reserve aux types positionnels)");
  // un role sans libelle anglais, ou un membre qui n'est ni simple ni un role : refuse
  const p = profiles() as { roles: Record<string, unknown> };
  assert.throws(() => kindsOf({ ...p, roles: { zone: { label: "Zone", members_label: "x", members: ["simple"] } } }), /anglais/);
  assert.throws(() => kindsOf({ ...p, roles: { ...p.roles, zone: { ...(p.roles.zone as object), members: ["inconnu"] } } }), /ni simple ni un role/);
});

test("ordre : glisser une carte envoie son rang sur config ; sans rang, les centrales d'abord (comme l'interface locale)", async () => {
  const { buildReorder, compareDevices } = await import("../opencloudapp/server/src/device-families.js");
  clearDeviceFamilies();
  registerDeviceFamily(family);
  applyMessage(`${ROOT}/${BOX}/cover/Chambre_Parents/attrs`, '{"type":"volet","positional":true,"name":"Chambre Parents"}', true, 1);
  applyMessage(`${ROOT}/${BOX}/cover/Test_centrale/attrs`, '{"type":"volet","positional":false,"name":"Test centrale","role":"central","members":["Chambre_Parents"]}', true, 1);
  const f = familyById("openprofalux")!;
  const devs = [...boxState("openprofalux", BOX)!.devices.values()];
  assert.deepEqual(devs.sort((a, b) => compareDevices(f, a, b)).map((d) => d.name), ["Test centrale", "Chambre Parents"]);
  assert.deepEqual(buildReorder(f, { box: BOX, device: "Test_centrale" }, 1), { topic: `${ROOT}/${BOX}/cover/Test_centrale/config`, payload: '{"rank":1}', retain: false });
});

test("telecommandes : releve reel du M5 (connues, utilisee par un volet), sur la page du boitier ; supprimer, ignorer, reaccepter, renommer", () => {
  clearDeviceFamilies();
  registerDeviceFamily(family);
  // releve reel du M5 (0.2.16-remotestest), 2026-11-15
  const R = `${ROOT}/${BOX}`;
  for (const [p, v] of [
    ["remote/0000813/info", '{"serial":"0x0000813","name":"Chambre Parent"}'],
    ["remote/0001067/info", '{"serial":"0x0001067","name":"Chambre Parents","used_by":"Chambre Parents"}'],
    ["remote/1009224/info", '{"serial":"0x1009224"}'],
  ] as const) assert.deepEqual(applyMessage(`${R}/${p}`, v, true, 1)!.refused, [], p);
  const devs = boxState("openprofalux", BOX)!.devices, f = familyById("openprofalux")!;
  const used = devs.get("0001067")!, free = devs.get("0000813")!, bare = devs.get("1009224")!;
  assert.deepEqual([used.kind, used.name, [...used.hidden].sort(), used.values.get("used_by")], ["remote", "Chambre Parents", ["ignore", "remove"], "Chambre Parents"]);
  assert.deepEqual([free.name, [...free.hidden], free.settings.get("remote_name")], ["Chambre Parent", ["used_by"], "Chambre Parent"]);
  assert.deepEqual([bare.name, bare.settings.get("remote_name")], ["0x1009224", ""], "sans nom : son serial");
  assert.equal(f.kinds.find((k) => k.id === "remote")!.place, "box", "rangee sur la page du boitier");
  const t = (device: string) => ({ box: BOX, device });
  assert.deepEqual(buildCommand(f, t("0000813"), "remove", null), { topic: `${R}/remote/0000813/set`, payload: "DELETE", retain: false });
  assert.deepEqual(buildCommand(f, t("0000813"), "ignore", null), { topic: `${R}/remote/0000813/set`, payload: "IGNORE", retain: false });
  assert.deepEqual(buildConfigure(f, t("0000813"), { remote_name: "Salon" }), { topic: `${R}/remote/0000813/config`, payload: '{"name":"Salon"}', retain: false });
  assert.ok("error" in buildConfigure(f, t("0000813"), { remote_name: "x".repeat(24) }), "nom trop long pour le firmware");
  // ignoree puis reacceptee : sa carte change d'onglet... puis revient
  assert.deepEqual(applyMessage(`${R}/remote/0000813/info`, "", true, 1)!.refused, []);
  applyMessage(`${R}/ignored/0000813`, '{"serial":"0x0000813"}', true, 1);
  assert.ok(!devs.has("0000813") && devs.get("ignored-0000813")!.kind === "remote-ignored");
  assert.deepEqual(buildCommand(f, t("ignored-0000813"), "accept", null), { topic: `${R}/ignored/0000813/set`, payload: "ACCEPT", retain: false });
  applyMessage(`${R}/ignored/0000813`, "", true, 1);
  assert.ok(!devs.has("ignored-0000813"));
  // un volet supprime sur le boitier (description videe) disparait aussi
  applyMessage(`${R}/cover/Test_centrale/attrs`, '{"type":"volet","positional":false,"name":"Test centrale","role":"central","members":[]}', true, 1);
  applyMessage(`${R}/cover/Test_centrale/attrs`, "", true, 1);
  assert.ok(!devs.has("Test_centrale"));
});

test("sauvegarde : demandee sur backup/get, la reponse du boitier (backup/data) remise a qui l'attend ; retenue, refusee", async () => {
  clearDeviceFamilies();
  registerDeviceFamily(family);
  const f = familyById("openprofalux")!;
  assert.deepEqual(buildFileRequest(f, BOX, "backup"), { topic: `${ROOT}/${BOX}/backup/get`, payload: "GET", retain: false });
  // forme reelle de /api/backup du M5 (2026-11-15), valeurs d'essai
  const sauv = '{"log_frames":true,"remotes":{"0x0001067":"Chambre Parents"},"volets":[{"id":"Chambre Parents","serials":[],"cmd":{"up":"","down":"","stop":""},"virt":true}],"pfx_enrol":[{"remote":"0x0001067","slot":1,"counter":12}]}';
  const attente = awaitBoxFile("openprofalux", BOX, "backup", 1000);
  assert.match(applyMessage(`${ROOT}/${BOX}/backup/data`, sauv, true, 1)!.refused.join(), /retenu/);
  assert.deepEqual(applyMessage(`${ROOT}/${BOX}/backup/data`, sauv, false, 1)!.refused, []);
  assert.equal(await attente, sauv);
});

test("cloud coupe sur le boitier : presence « disabled » annoncee par le firmware, distincte d'une panne", () => {
  assert.deepEqual(read({ box: BOX, path: "status", payload: "disabled", retained: true }), [{ fact: FACT.presence, online: false, disabled: true }]);
  assert.deepEqual(read({ box: BOX, path: "status", payload: "offline", retained: true }), [{ fact: FACT.presence, online: false }]);
});
test("boitier oublie sur lui-meme : presence « forgotten », le portail le retire du compte", () => {
  assert.deepEqual(read({ box: BOX, path: "status", payload: "forgotten", retained: true }), [{ fact: FACT.presence, online: false, forgotten: true }]);
});


test("carte declaree : le target du firmware (releve reel du M5), pour la fiche operateur", () => {
  clearDeviceFamilies();
  registerDeviceFamily(family);
  applyMessage(`${ROOT}/${BOX}/system/status`, '{"fw":"0.2.16-sauvtest","target":"m5-atom-lite","free_heap":30000}', true, 1);
  assert.equal(boxState("openprofalux", BOX)!.board, "m5-atom-lite");
});

test("boitier retire du compte : ordre d'oubli sous cloud/forget, que le firmware ecoute", () => {
  assert.deepEqual(family.unpair?.({ box: BOX }), { path: "cloud/forget", payload: "FORGET" });
});

test("nom du boitier : transmis sous cloud/name, que le firmware affiche", () => {
  assert.deepEqual(family.boxName?.({ box: BOX }, "Maison"), { path: "cloud/name", payload: "Maison" });
});
