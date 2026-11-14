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
import { FACT } from "../opencloudapp/server/src/family-contract.js";
import { registerDeviceFamily, clearDeviceFamilies, applyMessage, boxState, buildCommand, buildConfigure, familyById } from "../opencloudapp/server/src/device-families.js";

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

test("le releve reel d'un boitier donne : en ligne, un volet positionnel a 0 %, sa version", () => {
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
  assert.deepEqual([d.kind, d.known, d.name, [...d.hidden], d.values.get("position")], ["volet", true, "Chambre Parents", [], 0]);
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
  assert.deepEqual(buildCommand(f, target, "position", 42), { topic: `${ROOT}/${BOX}/cover/Chambre_Parents/set_position`, payload: "42", retain: false });
  assert.deepEqual(buildCommand(f, { box: BOX }, "update", null), { topic: `${ROOT}/${BOX}/update/install`, payload: "INSTALL", retain: false });
  assert.deepEqual(Object.keys(ACTION_PAYLOAD).sort(), ["down", "stop", "up"]);
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
  assert.deepEqual(family.kinds.map((k) => k.id), kinds.map((k) => k.id), "le plugin porte les types du firmware");
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
});

test("etalonnage : un volet non etalonne n'a pas de valeurs ; un portail n'a pas de reglages", () => {
  clearDeviceFamilies();
  registerDeviceFamily(family);
  applyMessage(`${ROOT}/${BOX}/cover/Chambre_Parents/attrs`, '{"type":"volet","positional":false,"name":"Chambre Parents"}', true, 1);
  assert.equal(boxState("openprofalux", BOX)!.devices.get("Chambre_Parents")!.settings.size, 0);
  assert.equal(family.kinds.find((k) => k.id === "portail")!.settings, undefined);
  const volet = family.kinds.find((k) => k.id === "volet")!;
  assert.deepEqual(volet.settings!.map((x) => [x.key, "min" in x ? x.min : null, "max" in x ? x.max : null]), [["travel_up_s", 1, 180], ["travel_down_s", 1, 180]], "bornes de profiles.json");
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
  assert.deepEqual(read({ box: BOX, path: "listen/state", payload: "ON", retained: true }), [], "l'ecoute radio n'est pas un battement");
  assert.deepEqual(read({ box: BOX, path: "openprofalux/state", payload: '{"last_cmd":"X"}', retained: true }), [], "sans signal, ce n'est pas un battement");
});
