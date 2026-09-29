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
        { "crop": [x, y, w, h], "rotate": 0|90|180|270, "knobs": true|false }
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

    class RealPhotos
    {
    public:
        struct Photo
        {
            juce::Image image;          // composizione alla dimensione dell'immagine della replica (2x)
            bool knobs = true;          // disegna i pomelli 3D sopra la foto
            juce::File file;
        };

        /** Cartella delle foto personali (creata al primo uso). */
        static juce::File folder();
        /** Foto personale per la replica 'real' (non valida se assente o rifiutata). */
        Photo forModel (const engine::ModelDef& real);
        /** Ultimo errore per il modello (vuoto se nessuno). */
        juce::String problemFor (const char* id) const;
        /** Svuota la cache (dopo aver cambiato le foto nella cartella). */
        void reload();

        /** Verifiche di sicurezza sul file (estensione, firma, dimensioni dichiarate). Esposte per l'autotest. */
        static juce::String checkImageFile (const juce::File&, int& width, int& height);
        /** Composizione in rilievo della foto sul piano della replica. Esposta per l'autotest. */
        static juce::Image compose (const juce::Image& photo, const engine::ModelDef& real, juce::Rectangle<int> crop, int rotate);

    private:
        struct Entry { juce::String key; Photo photo; juce::String error; };
        std::map<juce::String, Entry> cache;
    };
}
