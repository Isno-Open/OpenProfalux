#!/usr/bin/env bash
# Banc de simulation : le VRAI firmware dans QEMU (ESP32), relie EN MEME TEMPS a un broker
# local (le role de Home Assistant) et a un broker cloud mTLS (le role du broker ISNO),
# comme un boitier installe. Il verifie que les deux connexions vivent en parallele :
#   - le battement (~60 s) arrive sur les DEUX brokers ;
#   - le cloud recoit la description des volets et la version, jamais de trame radio.
#
# Usage : ./test/qemu/run.sh [duree_observation_s]   (depuis firmware/, docker requis)
# Sortie : 0 si tout est vu, 1 sinon. Journal du boitier : build-qemu/sim/boitier.log
#
# Tout est jetable : autorite de certification de test, brokers et enrolement vivent dans
# build-qemu/sim et dans des conteneurs supprimes a la fin. Rien ne touche le vrai cloud.
set -euo pipefail
cd "$(dirname "$0")/../.."
FW="$(pwd)"; ROOT="$(cd .. && pwd)"
OBSERVE_S="${1:-150}"
IDF_IMAGE="${IDF_IMAGE:-espressif/idf:v6.1}"
MQTT_IMAGE="${MQTT_IMAGE:-eclipse-mosquitto:2.0.22}"
BUILD="build-qemu"; SIM="$BUILD/sim"
# Ports sur la machine ; depuis QEMU (reseau utilisateur), la machine est 10.0.2.2.
HOST_FROM_GUEST="10.0.2.2"
PORT_LOCAL=11883 PORT_CLOUD=18883 PORT_ENROLL=18081 PORT_WEB=18080
DEVICE_ID="opf-sim0001"
HEARTBEAT="HEARTBEAT"
# Ce qui doit rester local (miroir de CLOUD_LOCAL_ONLY dans main/cloud_topic.c, verifie
# ici de bout en bout, sur ce qui arrive vraiment au broker).
LOCAL_ONLY_RE='/(frames|counter|busy)/|/log$'
C_QEMU=opf-sim-qemu C_LOCAL=opf-sim-local C_CLOUD=opf-sim-cloud C_ENROLL=opf-sim-enroll
UIDGID="$(id -u):$(id -g)"

cleanup() { [ -d "$SIM" ] && docker logs "$C_QEMU" > "$SIM/boitier.log" 2>&1 || true
            docker rm -f "$C_QEMU" "$C_LOCAL" "$C_CLOUD" "$C_ENROLL" >/dev/null 2>&1 || true; }
trap cleanup EXIT
cleanup

idf() { docker run --rm -u "$UIDGID" -e HOME=/tmp -e OPF_VERSION="${OPF_VERSION:-0.0.0-qemutest}" \
          -v "$ROOT":/project -w /project/firmware --entrypoint bash "$IDF_IMAGE" \
          -c ". /opt/esp/idf/export.sh >/dev/null 2>&1; $1"; }

echo ">> construction (carte de simulation test/qemu/board.json)"
CFG="-DSDKCONFIG=$BUILD/sdkconfig -DBOARD_FILE=/project/firmware/test/qemu/board.json"
[ -f "$BUILD/sdkconfig" ] || idf "idf.py -B $BUILD $CFG -DSDKCONFIG_DEFAULTS='sdkconfig.defaults;test/qemu/sdkconfig.qemu' set-target esp32 >/dev/null"
idf "idf.py -B $BUILD $CFG build >/dev/null && cd $BUILD && python -m esptool --chip esp32 merge-bin --fill-flash-size 4MB -o flash-qemu.bin @flash_args >/dev/null"

echo ">> autorite de certification de test"
rm -rf "$SIM"; mkdir -p "$SIM"
( cd "$SIM"
  openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj "/CN=opf-sim-ca" -keyout ca.key -out ca.crt 2>/dev/null
  sign() {  # $1 = nom de fichier, $2 = CN, $3 = extensions eventuelles
    openssl req -newkey rsa:2048 -nodes -subj "/CN=$2" -keyout "$1.key" -out "$1.csr" 2>/dev/null
    if [ -n "${3:-}" ]; then printf '%s\n' "$3" > "$1.ext"; EXT=(-extfile "$1.ext"); else EXT=(); fi
    openssl x509 -req -in "$1.csr" -CA ca.crt -CAkey ca.key -CAcreateserial -days 2 "${EXT[@]}" -out "$1.crt" 2>/dev/null
  }
  sign server localhost "subjectAltName=IP:$HOST_FROM_GUEST,IP:127.0.0.1,DNS:localhost"
  sign device "$DEVICE_ID"
  sign relay relay
  chmod 644 ./*.key )

echo ">> brokers : local (role HA) et cloud (mTLS, ACL par boitier)"
cat > "$SIM/local.conf" <<EOF
listener $PORT_LOCAL
allow_anonymous true
EOF
cat > "$SIM/acl" <<EOF
user relay
topic readwrite openprofalux/#
pattern readwrite openprofalux/%u/#
EOF
cat > "$SIM/cloud.conf" <<EOF
per_listener_settings true
listener $PORT_CLOUD
cafile /sim/ca.crt
certfile /sim/server.crt
keyfile /sim/server.key
require_certificate true
use_identity_as_username true
allow_anonymous false
acl_file /sim/acl
EOF
docker run -d --name "$C_LOCAL" --network host -v "$FW/$SIM":/sim:ro "$MQTT_IMAGE" mosquitto -c /sim/local.conf >/dev/null
docker run -d --name "$C_CLOUD" --network host -v "$FW/$SIM":/sim:ro "$MQTT_IMAGE" mosquitto -c /sim/cloud.conf >/dev/null

echo ">> enrolement de test (rend le paquet que rendrait le portail)"
python3 - "$SIM" "$DEVICE_ID" "mqtts://$HOST_FROM_GUEST:$PORT_CLOUD" > "$SIM/bundle.json" <<'PY'
import json, sys
d = sys.argv[1]
read = lambda n: open(f"{d}/{n}").read()
print(json.dumps({"device_id": sys.argv[2], "broker_url": sys.argv[3],
                  "ca": read("ca.crt"), "cert": read("device.crt"), "key": read("device.key")}))
PY
cat > "$SIM/enroll.py" <<'PY'
import http.server, sys
BUNDLE = open(sys.argv[2], "rb").read()
class H(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        self.rfile.read(int(self.headers.get("Content-Length", 0)))
        ok = self.path == "/api/enroll"
        self.send_response(200 if ok else 404)
        self.send_header("Content-Type", "application/json"); self.end_headers()
        if ok: self.wfile.write(BUNDLE)
http.server.HTTPServer(("0.0.0.0", int(sys.argv[1])), H).serve_forever()
PY
docker run -d --name "$C_ENROLL" --network host -v "$FW/$SIM":/sim:ro --entrypoint python3 "$IDF_IMAGE" \
  /sim/enroll.py "$PORT_ENROLL" /sim/bundle.json >/dev/null

# La flash emulee est un fichier : la config ecrite par le boitier survit a l'arret de QEMU.
qemu_start() {
  docker run -d --name "$C_QEMU" --network host -u "$UIDGID" -v "$ROOT":/project -w "/project/firmware/$BUILD" \
    --entrypoint bash "$IDF_IMAGE" -c ". /opt/esp/idf/export.sh >/dev/null 2>&1; exec qemu-system-xtensa -nographic \
    -machine esp32 -m 4M -drive file=flash-qemu.bin,if=mtd,format=raw \
    -nic user,model=open_eth,hostfwd=tcp::$PORT_WEB-:80" >/dev/null; }
# Redemarrage A FROID : QEMU reprend mal un redemarrage logiciel de l'ESP32 (panique
# InstrFetchProhibited a l'initialisation de la flash, avant tout code du firmware), ce que
# le materiel fait tres bien. On arrete donc QEMU et on le relance, comme une coupure secteur.
qemu_cold_restart() { docker logs "$C_QEMU" > "$SIM/boitier-avant.log" 2>&1 || true
                      docker rm -f "$C_QEMU" >/dev/null; qemu_start; }
echo ">> boitier emule (QEMU)"
qemu_start
WEB="http://127.0.0.1:$PORT_WEB"
wait_web() { for _ in $(seq 1 120); do curl -s -m3 "$WEB/api/cloud/status" >/dev/null && return 0; sleep 2; done
             echo "ECHEC : interface web muette"; return 1; }
wait_web

echo ">> appairage cloud, broker local, puis redemarrage : les deux connexions partent ensemble"
curl -s -m120 -X POST "$WEB/api/cloud/pair" -H 'Content-Type: application/json' \
  -d "{\"code\":\"sim\",\"server\":\"http://$HOST_FROM_GUEST:$PORT_ENROLL\"}"; echo
curl -s -m10 -X POST "$WEB/api/config" -H 'Content-Type: application/json' \
  -d "{\"mqtt_uri\":\"mqtt://$HOST_FROM_GUEST:$PORT_LOCAL\"}"; echo
sleep 3   # la reponse part apres l'ecriture en NVS ; marge pour la flash emulee
qemu_cold_restart; wait_web
BOOT_MARK=1

echo ">> observation des deux brokers pendant $OBSERVE_S s"
sub() {  # $1 = conteneur de reference (reseau), $@ = options mosquitto_sub
  docker run --rm --network host -v "$FW/$SIM":/sim:ro "$MQTT_IMAGE" mosquitto_sub -v -W "$OBSERVE_S" "$@" 2>/dev/null || true; }
sub -h 127.0.0.1 -p "$PORT_LOCAL" -t 'openprofalux/#' > "$SIM/local.txt" &
sub -h 127.0.0.1 -p "$PORT_CLOUD" --cafile /sim/ca.crt --cert /sim/relay.crt --key /sim/relay.key \
    -t 'openprofalux/#' > "$SIM/cloud.txt" &
wait
docker logs "$C_QEMU" > "$SIM/boitier.log" 2>&1 || true

echo "== ce que le boitier a dit depuis le redemarrage"
tail -n +"$BOOT_MARK" "$SIM/boitier.log" | grep -E "mqtt|MQTT|cloud|PRET|Guru|abort" | cut -c1-160 | tail -20
echo "== topics recus"
for f in local cloud; do echo "-- $f"; awk '{print $1}' "$SIM/$f.txt" | sort | uniq -c; done

FAIL=0
check() { if eval "$2"; then echo "OK   $1"; else echo "KO   $1"; FAIL=1; fi; }
check "battement sur le broker local"  "grep -q '$HEARTBEAT' $SIM/local.txt"
check "battement sur le broker cloud"  "grep -q '$HEARTBEAT' $SIM/cloud.txt"
check "boitier en ligne sur le cloud"  "grep -q '^openprofalux/$DEVICE_ID/status online' $SIM/cloud.txt"
check "version publiee au cloud"       "grep -q '^openprofalux/$DEVICE_ID/system/status ' $SIM/cloud.txt"
check "aucune trame radio vers le cloud" "! grep -E -q '$LOCAL_ONLY_RE' <(awk '{print \$1}' $SIM/cloud.txt)"
[ "$FAIL" = 0 ] && echo "RESULTAT : OK" || echo "RESULTAT : KO"
exit "$FAIL"
