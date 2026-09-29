/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Modalita' REAL MOD PEDALBOARD.

    Con il tasto REAL MOD la pedaliera mostra, al posto dei pedali originali
    Pedal Trinity, le repliche fedeli dei pedali reali (forma, colori, nome e
    sigla, disposizione dei comandi), renderizzate in 3D da tools/art. Suono,
    comandi e preset non cambiano: e' solo un altro aspetto degli stessi slot.

    Foto personali: chi vuole puo' mettere le proprie foto dei pedali nella
    cartella RealPhotos (vedi RealPhotos::folder()). Le foto non fanno parte
    del programma: restano sul computer dell'utente. Nomi ammessi:
        <id>.jpg / .jpeg / .png        (es. ds1.jpg)
        <SIGLA>.jpg / .jpeg / .png     (es. DS-1.jpg)
    e, facoltativo, un file <stesso nome>.json:
        { "crop": [x, y, w, h], "rotate": 0|90|180|270, "knobs": true|false,
          "controls": { "LEVEL": [u, v, r], ... }, "led": [u, v] }
    "controls" indica dove sono i pomelli nella foto: i pomelli 3D regolabili vi si
    sovrappongono esattamente, alla stessa dimensione.
    La foto (vista dall'alto) viene ritagliata, adagiata sul piano superiore
    della replica con fronte, bordi, luce e ombra (rilievo 3D) e i comandi
    restano regolabili sopra di essa.
*/

#pragma once

#include <map>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/Model.h"

namespace pt::ui
{
    /** Definizione da disegnare: la replica reale se REAL MOD e' attivo e il modello ne ha una. */
    const engine::ModelDef* visualDef (const engine::ModelDef*);

    /** Pomello nella foto: centro (x = u, y = v) e raggio z, in frazioni della foto ritagliata. */
    struct PhotoKnob { float x = 0, y = 0, z = 0; };

    class RealPhotos
    {
    public:
        struct Photo
        {
            juce::Image image;          // composizione alla dimensione dell'immagine della replica (2x)
            bool knobs = true;          // disegna i pomelli 3D sopra la foto
            juce::File file;
            /** Pomelli della foto: etichetta del comando -> centro (u, v) e raggio r, in frazioni
                della foto ritagliata (u, r sulla larghezza, v sull'altezza). I pomelli 3D vi si sovrappongono. */
            std::vector<std::pair<juce::String, PhotoKnob>> controls;
            bool hasLed = false;        // LED CHECK della foto (u, v in frazioni della foto)
            float ledU = 0, ledV = 0;
        };

        /** Cartella delle foto personali (creata al primo uso, con ELENCO_FOTO.txt dei nomi accettati). */
        static juce::File folder();
        /** Scrive ELENCO_FOTO.txt: per ogni pedale i nomi di file accettati. */
        static void writeList (const juce::File& dir);
        /** Foto personale per la replica 'real' (non valida se assente o rifiutata). */
        Photo forModel (const engine::ModelDef& real);
        /** Ultimo errore per il modello (vuoto se nessuno). */
        juce::String problemFor (const char* id) const;
        /** Svuota la cache (dopo aver cambiato le foto nella cartella). */
        void reload();
        /** Disattiva le foto personali (screenshot per la guida: mai foto dell'utente nei documenti pubblici). */
        static void setEnabled (bool e) { enabledFlag() = e; }
        static bool isEnabled() { return enabledFlag(); }

        /** Verifiche di sicurezza sul file (estensione, firma, dimensioni dichiarate). Esposte per l'autotest. */
        static juce::String checkImageFile (const juce::File&, int& width, int& height);
        /** Lettura del file .json di una foto (crop, rotate, knobs, led, controls): errore o stringa vuota. */
        static juce::String parseSidecar (const juce::var&, juce::Rectangle<int> imageBounds, Photo&,
                                          juce::Rectangle<int>& crop, int& rotate, bool& knobs);
        /** Salva nel .json della foto le posizioni dei pomelli (e del LED) allineate nel pannello di zoom. */
        static juce::String saveAlignment (const juce::File& photoFile, const std::vector<std::pair<juce::String, PhotoKnob>>&,
                                           bool hasLed, float ledU, float ledV);
        /** Composizione in rilievo della foto sul piano della replica. Esposta per l'autotest. */
        static juce::Image compose (const juce::Image& photo, const engine::ModelDef& real, juce::Rectangle<int> crop, int rotate);

    private:
        static bool& enabledFlag() { static bool e = true; return e; }
        struct Entry { juce::String key; Photo photo; juce::String error; };
        std::map<juce::String, Entry> cache;
    };
}
