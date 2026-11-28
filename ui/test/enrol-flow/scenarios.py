"""Banc de l'ecran d'enrolement (issue #159) : rejoue les gestes d'un utilisateur dans un vrai navigateur, sur
une interface servie par le faux boitier, et compare ce qu'il a saisi a ce que le boitier emet.

    python3 scenarios.py <adresse du faux boitier> <current|legacy> <pass|fail>

`current` pilote l'interface du depot, `legacy` celle de la 0.2.16 (le temoin : le banc DOIT y echouer, sinon
il ne prouve rien). `pass` attend que tous les scenarios passent, `fail` que ceux de MUST_FAIL echouent.
"""
import json, sys, urllib.request
from playwright.sync_api import sync_playwright

BASE, UI, EXPECT = sys.argv[1], sys.argv[2], sys.argv[3]
MEASURED = {"freq_khz": 868330, "te_us": 439}   # porteuse et TE du rapport #159, a cote du profil (868425 kHz, 400 us)
BRAND, FLOW = "Profalux", "enrol-pfx"
RC_NOTHING = -3
EMISSION_MS = 7500

def get(path):
    return json.load(urllib.request.urlopen(BASE + path))
def post(path, payload):
    urllib.request.urlopen(urllib.request.Request(BASE + path, data=json.dumps(payload).encode(), headers={"Content-Type": "application/json"}))
def profile_index():
    return next(i for i, p in enumerate(get("/profiles.json")["profils"]) if p["marque"] == BRAND and p["flow"] == FLOW)

class Legacy:
    """Interface 0.2.16 (firmware/main/web/app.js)."""
    def open(self, page):
        page.goto(BASE + "/"); page.wait_for_timeout(1500)
        page.evaluate(f"(async () => {{ if (!PROFILES) PROFILES = await fetch('/profiles.json').then(r => r.json()); routeLeaf(PROFILES.profils[{profile_index()}]); }})()")
        page.wait_for_selector('#pfx-advanced .advf[data-name="freq_khz"]', state="attached")
    def panel(self, page):
        return page.evaluate("""() => Object.fromEntries([...document.querySelectorAll('#pfx-advanced .advf')].map(e => [e.dataset.name, Number(e.value)]))""")
    def type(self, page):
        page.evaluate("document.querySelector('#pfx-advanced details').open = true")
        for name, value in MEASURED.items(): page.fill(f'#pfx-advanced .advf[data-name="{name}"]', str(value))
    def capture(self, page, ok):
        page.click("#pfx-capture")
        page.wait_for_function(f"document.querySelector('#pfx-capture-status b').textContent.includes('{'✅' if ok else '❌'}')", timeout=30000)
        page.wait_for_timeout(500)
    def leave_and_return(self, page):
        page.evaluate("location.hash = 'control'"); page.wait_for_timeout(800)
        page.evaluate("location.hash = 'remotes/enrol'"); page.wait_for_timeout(800)
    def emit(self, page):
        page.wait_for_selector("#pfx-learn:not([disabled])"); page.click("#pfx-learn"); page.wait_for_timeout(EMISSION_MS)

class Current:
    """Interface du depot (ui/, Vue)."""
    def route(self): return f"remotes/enrol/{profile_index()}"
    def open(self, page):
        page.goto(f"{BASE}/#{self.route()}")
        page.wait_for_selector('.tx-params input[id$="-freq_khz"]', state="attached")
        page.wait_for_timeout(500)
    def panel(self, page):
        return page.evaluate("""() => Object.fromEntries([...document.querySelectorAll('.tx-params input[type=number]')].map(e => [e.id.replace(/^.*-/, ''), Number(e.value)]))""")
    def type(self, page):
        if not page.evaluate("document.querySelector('.tx-params details').open"): page.click(".tx-params summary")
        for name, value in MEASURED.items(): page.fill(f'.tx-params input[id$="-{name}"]', str(value))
    def capture(self, page, ok):
        page.get_by_role("button", name="apturer").click()
        page.get_by_text("attribuée" if ok else "a échoué").first.wait_for(timeout=30000)
        page.wait_for_timeout(500)
    def leave_and_return(self, page):
        page.evaluate("location.hash = 'control'"); page.wait_for_timeout(800)
        page.evaluate(f"location.hash = '{self.route()}'")
        page.wait_for_selector('.tx-params input[id$="-freq_khz"]', state="attached"); page.wait_for_timeout(800)
    def emit(self, page):
        button = page.get_by_role("button", name="Émettre la trame")
        button.wait_for(); page.wait_for_function("[...document.querySelectorAll('button')].some(b => b.textContent.includes('Émettre la trame') && !b.disabled)")
        button.click(); page.wait_for_timeout(EMISSION_MS)

TYPE, FAILED_CAPTURE, CAPTURE, LEAVE, EMIT = "saisir", "capture ratee", "capture", "autre onglet", "emettre"
SCENARIOS = [
    ("A capture, emission", [TYPE, CAPTURE, EMIT]),
    ("D capture ratee, puis recapture sans rien retoucher", [TYPE, FAILED_CAPTURE, CAPTURE, EMIT]),
    ("E capture ratee, valeurs ressaisies, recapture", [TYPE, FAILED_CAPTURE, TYPE, CAPTURE, EMIT]),
    ("F aller-retour sur un autre onglet avant la capture", [TYPE, LEAVE, CAPTURE, EMIT]),
    ("G aller-retour sur un autre onglet apres la capture", [TYPE, CAPTURE, LEAVE, EMIT]),
    ("H valeurs saisies apres la capture seulement", [CAPTURE, TYPE, EMIT]),
]
MUST_FAIL = {"legacy": ["A", "D", "F", "H"]}   # ce que le temoin doit montrer : la saisie perdue (A) et l'emission au profil (D, F, H)

def run(driver, name, steps):
    """Rend la liste des ecarts : panneau qui ne montre plus la saisie, ou emission differente de la saisie."""
    with sync_playwright() as p:
        browser = p.chromium.launch(); page = browser.new_page()
        start = len(get("/__log"))
        post("/api/pfx/forget", {})   # session d'avant close
        driver.open(page)
        gaps, typed = [], False
        for step in steps:
            if step == TYPE: driver.type(page)
            elif step == FAILED_CAPTURE: post("/__next_capture", [RC_NOTHING]); driver.capture(page, False)
            elif step == CAPTURE: driver.capture(page, True)
            elif step == LEAVE: driver.leave_and_return(page)
            elif step == EMIT: driver.emit(page)
            typed = typed or step == TYPE
            shown = {k: v for k, v in driver.panel(page).items() if k in MEASURED}
            if typed and shown != MEASURED: gaps.append(f"apres « {step} », le panneau montre {shown}")
        emissions = [{k: e[k] for k in MEASURED} for e in get("/__log")[start:] if e["kind"] == "emission"]
        if emissions != [MEASURED]: gaps.append(f"emission {emissions}, attendu {[MEASURED]}")
        browser.close()
        return gaps

if __name__ == "__main__":
    driver = {"current": Current, "legacy": Legacy}[UI]()
    failed = []
    for name, steps in SCENARIOS:
        gaps = run(driver, name, steps)
        print(("  FAIL " if gaps else "  ok   ") + name)
        for g in gaps: print("         " + g)
        if gaps: failed.append(name[0])
    if EXPECT == "pass":
        print("ECHEC : %d scenario(s)" % len(failed) if failed else "Tout passe")
        sys.exit(1 if failed else 0)
    missing = [s for s in MUST_FAIL[UI] if s not in failed]
    print("ECHEC : le temoin ne reproduit pas %s" % missing if missing else "Temoin : le defaut est reproduit (%s)" % ", ".join(failed))
    sys.exit(1 if missing else 0)
