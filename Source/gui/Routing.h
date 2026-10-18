/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Regole dei cavi, comuni alla pedaliera e alla Mappa di Routing.

    I collegamenti si ricavano sempre dalla catena (stessa fonte di verita'
    del motore audio: pedali in fila, al massimo uno splitter, corsie A/B in
    DUAL, pedali staccati saltati). Staccare o ricollegare una spina non crea
    collegamenti arbitrari: cambia solo quali pedali sono collegati
    (Chain::setPatched), con le stesse regole ovunque:
      - il segnale va da sinistra a destra (indice crescente della catena);
      - lo splitter non si stacca e non si scavalca;
      - in DUAL ogni cavo resta nella sua corsia;
      - spina lasciata nel vuoto = pedale a quell'estremita' staccato.

    RouteLayout: posizioni degli oggetti della mappa (salvate nello stato
    del progetto, non nei preset), che seguono gli slot quando la catena
    cambia (spostamenti, inserimenti, cambi di pedale).
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/Chain.h"

namespace pt::ui::routing
{
    /** Estremita' di un cavo. line: 0 = presa A, 1 = presa B. */
    struct End
    {
        enum Kind { Input, Output, SlotIn, SlotOut } kind;
        int slot = -1;
        int line = 0;
        bool operator== (const End& o) const { return kind == o.kind && slot == o.slot && line == o.line; }
    };
    /** Cavo: sorgente (uscita) -> destinazione (ingresso). */
    struct Conn { End a, b; };

    /** Cavi della catena, nell'ordine in cui li disegna la pedaliera (gli indici identificano i cavi). */
    std::vector<Conn> connections (const engine::Chain&);
    /** Collegamenti in forma compatta, es. "I>0 0>2 2>O" (I = INPUT, O = OUTPUT, numeri = slot). */
    juce::String describe (const std::vector<Conn>&);

    /** true se lo splitter c'e' ed e' in DUAL (corsie A/B separate). */
    bool isDual (const engine::Chain&);
    /** Tratto della catena: 0 = prima dello splitter (o catena senza corsie), 1/2 = corsia A/B in DUAL. */
    int segmentOfSlot (const engine::Chain&, int slot);
    int segmentOf (const engine::Chain&, const End&);

    /** Dove viene rilasciata la spina. */
    struct Target
    {
        enum Kind { Slot, InputPanel, OutputPanel, Empty, Outside } kind = Outside;
        int slot = -1;
    };

    /** Esito di un rilascio: cosa cambierebbe nella catena. */
    struct Replug
    {
        enum Kind { Cancel, Unplug, Plug } kind = Cancel;
        bool ok = false;
        std::vector<int> unpatch;       // pedali che il cavo smette di attraversare
        int patch = -1;                 // pedale che entra nella catena
        juce::String hint;              // spiegazione (anche del rifiuto)
    };

    /** Regole dei cavi: dove finirebbe la spina 'destEnd' del cavo c rilasciata sul bersaglio t.
        outsideHint: messaggio per Target::Outside (pedaliera o mappa). */
    Replug resolve (const engine::Chain&, const Conn& c, bool destEnd, const Target& t,
                    const juce::String& outsideHint = "Fuori dalla pedaliera: il cavo torna al suo posto");
    /** Applica il rilascio alla catena; true se la catena cambia. */
    bool apply (engine::Chain&, const Replug&);
}

namespace pt::ui
{
    /** Posizioni degli oggetti della Mappa di Routing (coordinate della mappa, px a zoom 1).
        Gli slot senza posizione salvata seguono la disposizione automatica.
        Segue gli slot per identita': spostamenti e inserimenti non scambiano le posizioni,
        un cambio di pedale nello stesso slot la conserva. Thread-safe (lo stato del progetto
        puo' essere letto dall'host da un altro thread). */
    class RouteLayout : private juce::ChangeListener
    {
    public:
        explicit RouteLayout (engine::Chain&);
        ~RouteLayout() override;

        /** Allinea le posizioni alla catena attuale (chiamata da sola quando la catena cambia). */
        void sync();

        bool slotPos (int index, juce::Point<float>& out) const;
        void setSlotPos (int index, juce::Point<float>);
        /** Oggetti INPUT (which = 0) e OUTPUT (which = 1). */
        bool ioPos (int which, juce::Point<float>& out) const;
        void setIoPos (int which, juce::Point<float>);
        /** Torna alla disposizione automatica (tutte le posizioni dimenticate). */
        void clearPositions();
        int numPlaced() const;

        /** Vista della mappa: zoom (0 = non ancora scelto, adatta alla prima apertura) e angolo in alto a sinistra. */
        float zoom() const;
        juce::Point<float> viewOrigin() const;
        void setView (float zoom, juce::Point<float> origin);

        /** Stato del progetto: nodo "ROUTEMAP" (assente nei progetti vecchi = disposizione automatica). */
        juce::ValueTree toValueTree() const;
        /** Ripristina dal progetto; un nodo non valido azzera (disposizione automatica). */
        void fromValueTree (const juce::ValueTree&);

        /** Chiave che cambia a ogni modifica delle posizioni (ridisegni). */
        juce::uint32 revision() const { return rev.load(); }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { sync(); }
        void syncLocked();
        struct Item { std::weak_ptr<engine::Slot> id; bool set = false; juce::Point<float> p; };

        engine::Chain& chain;
        mutable juce::CriticalSection lock;
        std::vector<Item> items;                 // allineato agli indici della catena all'ultima sync
        bool ioSet[2] { false, false };
        juce::Point<float> io[2];
        float viewZoom = 0.0f;
        juce::Point<float> origin;
        std::atomic<juce::uint32> rev { 1 };
    };
}
