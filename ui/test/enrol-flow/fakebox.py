"""Faux boitier pour le banc de l'ecran d'enrolement (issue #159).

Sert une interface web (celle du depot, ou celle d'une ancienne version pour le temoin), repond aux lectures
avec des reponses de synthese (fixtures/), et tient la session d'enrolement avec enrol_tx.c LUI-MEME, compile
en bibliotheque : la regle n'est pas recopiee ici. Chaque emission radio simulee est journalisee avec la
porteuse et le TE que le boitier aurait emis.

    python3 fakebox.py <port> <dossier web> <libenrol_tx.so>
"""
import ctypes, json, os, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "fixtures")
TYPES = {".js": "application/javascript", ".css": "text/css", ".html": "text/html", ".json": "application/json",
         ".webmanifest": "application/manifest+json"}
CAPTURE_S = 1.0          # duree d'une capture simulee
RC_NOTHING = -3          # /api/pfx/capture/poll : rien recu (telecommande trop loin)
IDENTITY, REMOTE, NO_SERIAL = "0x0670013", "0x054CA93", "0x0000000"

class EnrolTx(ctypes.Structure):   # enrol_tx_t (enrol_tx.h)
    _fields_ = [("freq_khz", ctypes.c_uint32), ("te_us", ctypes.c_uint32), ("rpt", ctypes.c_uint8), ("rmt", ctypes.c_uint8)]
class TxParams(ctypes.Structure):  # tx_params_t (radio.h)
    _fields_ = [("freq_khz", ctypes.c_uint32), ("te_us", ctypes.c_uint32), ("rpt", ctypes.c_int8), ("rmt", ctypes.c_int8)]
TXOVR_INHERIT, TXOVR_OFF, TXOVR_ON = 0, 1, 2   # tx_override_t (tx_override.h)

def fixture(name):
    return json.load(open(os.path.join(FIXTURES, name + ".json"), encoding="utf-8"))

class Box:
    def __init__(self, lib_path):
        self.lib = ctypes.CDLL(lib_path)
        self.lib.enrol_tx_params.restype = TxParams
        self.lib.enrol_tx_emitted_te.restype = ctypes.c_uint32
        self.session, self.brand = EnrolTx(), ""
        self.lib.enrol_tx_clear(ctypes.byref(self.session))
        config = fixture("config")
        self.radio_khz, self.radio_te = config["freq_khz"], config["tx_te"]   # reglages globaux de la radio
        self.active = self.learn_ok = False
        self.capture_done, self.capture_rc, self.next_rc, self.counter = True, 0, [], 2
        self.log = []

    @staticmethod
    def request(body):
        """Reglages portes par une requete, comme enrol_tx_request (web_ui.c) : cle absente = heriter."""
        body = body if isinstance(body, dict) else {}
        number = lambda k: int(body[k]) if isinstance(body.get(k), (int, float)) and not isinstance(body.get(k), bool) else 0
        override = lambda k: TXOVR_INHERIT if k not in body else (TXOVR_ON if body[k] else TXOVR_OFF)
        return EnrolTx(number("freq_khz"), number("te_us"), override("tx_repeat_bit"), override("tx_rmt"))

    def state(self):
        return {"active": int(self.active), "learn_ok": int(self.learn_ok), "model": 0, "serial": IDENTITY, "idx": 3,
                "counter": self.counter, "remote": REMOTE if self.active else NO_SERIAL, "marque": self.brand,
                "tx": {"freq_khz": self.session.freq_khz, "te_us": self.session.te_us, "tx_rpt": self.session.rpt, "tx_rmt": self.session.rmt},
                "models": fixture("pfx_models")}

    def capture(self, body):
        request = self.request(body)
        self.lib.enrol_tx_capture(ctypes.byref(self.session), ctypes.byref(request))
        self.brand = body.get("marque", "") if isinstance(body, dict) else ""
        self.active = self.learn_ok = self.capture_done = False
        rc = self.next_rc.pop(0) if self.next_rc else 0
        def finish():
            time.sleep(CAPTURE_S)
            self.active = self.learn_ok = rc == 0
            self.capture_rc, self.capture_done, self.counter = rc, True, 2
        threading.Thread(target=finish, daemon=True).start()

    def learn(self, body):
        if not self.learn_ok: return {"ok": 0, "err": "capture d'abord la telecommande"}
        given = isinstance(body, dict) and len(body) > 0
        request = self.request(body)
        self.lib.enrol_tx_learn(ctypes.byref(self.session), ctypes.byref(request), ctypes.c_bool(given))
        if given and isinstance(body.get("marque"), str): self.brand = body["marque"]
        params = self.lib.enrol_tx_params(ctypes.byref(self.session))
        self.log.append({"kind": "emission", "freq_khz": params.freq_khz or self.radio_khz,
                         "te_us": self.lib.enrol_tx_emitted_te(ctypes.byref(params), ctypes.c_uint32(self.radio_te))})
        self.counter += 1
        return {"ok": 1}

    def end(self):
        self.lib.enrol_tx_clear(ctypes.byref(self.session)); self.brand = ""
        self.active = self.learn_ok = False

def handler(box, web):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *a): pass
        def reply(self, body, ctype="application/json", code=200):
            data = body if isinstance(body, bytes) else json.dumps(body).encode()
            self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(data)))
            self.end_headers(); self.wfile.write(data)
        def do_GET(self):
            path = self.path.split("?")[0]
            if path == "/__log": return self.reply(box.log)
            if path == "/api/pfx": return self.reply(box.state())
            if path == "/api/pfx/capture/poll":
                return self.reply({"done": int(box.capture_done), "rc": box.capture_rc, "new": 1, "serial": IDENTITY, "remote": REMOTE, "used_by": ""})
            if path.startswith("/api/"):
                name = path[5:].replace("/", "_")
                return self.reply(fixture(name) if os.path.exists(os.path.join(FIXTURES, name + ".json")) else {})
            file = os.path.join(web, "index.html" if path == "/" else path.lstrip("/"))
            if not os.path.isfile(file): return self.reply(b"", "text/plain", 404)
            self.reply(open(file, "rb").read(), TYPES.get(os.path.splitext(file)[1], "application/octet-stream"))
        def do_POST(self):
            size = int(self.headers.get("Content-Length") or 0)
            raw = self.rfile.read(size).decode() if size else ""
            try: body = json.loads(raw) if raw else None
            except ValueError: body = None
            path = self.path.split("?")[0]
            box.log.append({"kind": "request", "path": path, "body": body})
            if path == "/__next_capture": box.next_rc = list(body); return self.reply({"ok": 1})
            if path == "/api/pfx/capture": box.capture(body); return self.reply({"ok": 1})
            if path == "/api/pfx/learn": return self.reply(box.learn(body))
            if path in ("/api/pfx/forget", "/api/pfx/save_volet"): box.end()
            self.reply({"ok": 1})
    return Handler

if __name__ == "__main__":
    port, web, lib = int(sys.argv[1]), sys.argv[2], sys.argv[3]
    ThreadingHTTPServer(("127.0.0.1", port), handler(Box(lib), web)).serve_forever()
