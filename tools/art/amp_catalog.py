"""
Pedal Trinity - aiuti per i cataloghi di amplificatori e casse (tappa 6): catalog_amps.py e catalog_cabs.py, generati
da gen_catalog_amps.py, descrivono l'ASPETTO (look["amp"]: forma, misure, pannello vero con le posizioni dei comandi,
griglia, rivestimento, retro); la PARTE FUNZIONALE (famiglia, comandi, config, note) viene da
sound_stage6_amps_a.json / sound_stage6_amps_b.json (ampli) e sound_stage6_cabs.json (casse) quando contengono l'id,
altrimenti si usa il segnaposto costruito dai comandi della scheda (placeholder=True nel modello).
I comandi funzionali si posano sulle posizioni del pannello vero per etichetta (amp_layout.assign); le posizioni senza
comando restano comandi fissi modellati nel render.
"""
import catalog
import stomp_catalog
from catalog import model

CATEGORY_AMP, CATEGORY_CAB = "Amplificatori", "Casse"


def _functional(id, placeholder, notes, stereo=False):
    sd = stomp_catalog.sound().get(id)
    fam, ctrls, cfg = placeholder
    is_ph = True
    if sd:
        try:
            jc = [stomp_catalog.json_control(c) for c in sd.get("controls") or []]
            jf = sd.get("family")
            if jc and jf in catalog.FAMILIES:
                ctrls, fam, cfg = jc[:12], jf, str(sd.get("config") or "")
                is_ph = False
                if sd.get("notes"):
                    notes = str(sd["notes"])
                if "stereo" in sd:
                    stereo = bool(sd["stereo"])
        except Exception as e:
            stomp_catalog.SOUND_ERRORS.append("%s: %s" % (id, e))
    return fam, ctrls, cfg, notes, stereo, is_ph


def fit_kinds(ctrls, A):
    """Sugli ampli i comandi funzionali si posano sui comandi del pannello vero: i concentrici della scheda funzionale
    (messi per stare nelle 8 posizioni dei pedali) tornano pomelli separati, e un comando che cade su un cursore del
    pannello vero (EQ grafico del Mark) diventa un cursore. Valori, ruoli e gamme non cambiano."""
    import amp_layout
    out = [dict(c, kind="knob") if c["kind"] in ("outer", "inner") else dict(c) for c in ctrls]
    asg, _, _ = amp_layout.assign(dict(controls=out), A)
    ents = A.get("entries") or []
    for i, j in asg.items():
        if ents[j]["kind"] == "slider" and out[i]["kind"] == "knob":
            out[i]["kind"] = "slider"
    return out


def amp(id, code, name, inspired, real, colour, look, placeholder, notes, subtitle="AMPLIFIER"):
    """Amplificatore (testata, combo, rack): real = (sigla reale, nome reale senza marchio)."""
    fam, ctrls, cfg, notes, stereo, is_ph = _functional(id, placeholder, notes)
    ctrls = fit_kinds(ctrls, look["amp"])
    m = model(id, code, name, inspired, CATEGORY_AMP, fam, colour, ctrls, cfg, notes, style="box", subtitle=subtitle,
              stereo=stereo, look=look, real=dict(code=real[0], name=real[1]))
    m["placeholder"] = is_ph
    return m


def cab(id, code, name, inspired, real, colour, look, placeholder, notes, subtitle="CABINET"):
    """Cassa (o mobile di un combo): i comandi della famiglia CabIR stanno sulla pedaliera davanti alla cassa."""
    fam, ctrls, cfg, notes, stereo, is_ph = _functional(id, placeholder, notes)
    m = model(id, code, name, inspired, CATEGORY_CAB, fam, colour, ctrls, cfg, notes, style="box", subtitle=subtitle,
              stereo=stereo, look=look, real=dict(code=real[0], name=real[1]) if real else None)
    m["placeholder"] = is_ph
    return m
