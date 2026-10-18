/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Mappa di Routing: tutta la catena come oggetti su un piano (INPUT, ogni
    slot occupato con la miniatura del pedale, lo splitter con le uscite A/B,
    OUTPUT) collegati dai cavi.
      - gli oggetti si spostano liberamente col mouse (posizioni salvate nel
        progetto, RouteLayout); senza posizioni salvate vale la disposizione
        automatica (righe che vanno a capo, corsie A/B una sotto l'altra);
      - le spine dei cavi si staccano e si riagganciano con le stesse regole
        della pedaliera (routing::resolve): la mappa non cambia il motore,
        cambia solo quali pedali sono collegati (Chain::setPatched);
      - doppio clic su un pedale = zoom; clic sul LED = acceso / spento;
      - rotellina = scorre, Ctrl/Cmd + rotellina = zoom, Esc = chiude.
    La catena e' la stessa della pedaliera: ogni modifica si vede subito in
    entrambe.
*/

#pragma once

#include <map>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Routing.h"
#include "Theme.h"

namespace pt::ui
{
    class RoutingMap : public juce::Component, private juce::ChangeListener, private juce::Timer,
                       private juce::ScrollBar::Listener
    {
    public:
        RoutingMap (engine::Chain&, RouteLayout&);
        ~RoutingMap() override;

        std::function<void()> onClose;
        /** Doppio clic su un pedale: zoom dello slot. */
        std::function<void (int slot)> onZoom;

        /** Ricostruisce oggetti e cavi dalla catena (chiamata da sola quando la catena cambia). */
        void refresh();

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
        bool keyPressed (const juce::KeyPress&) override;

        //==================== vista
        float getZoom() const { return zoom; }
        void setZoom (float z, juce::Point<float> aroundScreen);
        /** Adatta lo zoom per vedere tutti gli oggetti (entro i limiti di leggibilita'). */
        void fitAll (bool limitForReading = true);
        /** Torna alla disposizione automatica. */
        void autoArrange();

        //==================== autotest e screenshot
        static constexpr int inputNode = -1, outputNode = -2;
        int numNodes() const { return (int) nodes.size(); }
        int numCables() const { return (int) conns.size(); }
        juce::String describeCables() const { return routing::describe (conns); }
        /** Riquadro di un oggetto (slot, inputNode o outputNode) in coordinate della mappa (vuoto se assente). */
        juce::Rectangle<float> nodeWorldBounds (int slot) const;
        /** Centro di un oggetto sullo schermo (coordinate del componente). */
        juce::Point<float> nodeScreenCentre (int slot) const;
        /** Un punto libero del piano (nessun oggetto vicino), in coordinate del componente. */
        juce::Point<float> emptyScreenPoint() const;
        /** Porta 'slot' (o un oggetto di I/O) in vista. */
        void scrollToNode (int slot);
        /** Rilascia la spina del cavo 'conn' nel punto p come farebbe il mouse (stessa via del mouse); true se la catena cambia. */
        bool dropPlugForTest (int conn, bool destEnd, juce::Point<float> p);
        /** Trascina un oggetto come col mouse (da un punto del suo corpo a 'to'). */
        void dragNodeForTest (int slot, juce::Point<float> to);
        /** Clic sul LED di un pedale (stessa via del mouse). */
        void clickLedForTest (int slot);
        /** Ultimo avviso mostrato (rifiuti dei cavi) e se era un errore. */
        juce::String lastNotice() const { return toast.text; }
        bool lastNoticeWasError() const { return ! toast.ok; }
        /** Coppie di colori (testo / fondo) usate dalla mappa: contrasto minimo WCAG con il tema. */
        static double minContrast (const Theme&);

    private:
        struct Node
        {
            int slot = -1;                          // indice della catena, inputNode, outputNode
            juce::Rectangle<float> world;           // posizione e dimensione sulla mappa (zoom 1)
            const engine::ModelDef* def = nullptr;  // modello (suono)
            const engine::ModelDef* vis = nullptr;  // aspetto (replica REAL MOD)
            int lane = -1;                          // -1 nessuna, 0 = A, 1 = B (DUAL)
            bool inB = false, outB = false;         // prese B in uso (splitter, pedali stereo, OUTPUT stereo)
        };
        enum class Drag { None, Plug, Node, Pan };

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void scrollBarMoved (juce::ScrollBar*, double) override;

        juce::Rectangle<int> card() const;
        juce::Rectangle<float> canvasArea() const { return canvas.toFloat(); }
        juce::Point<float> toScreen (juce::Point<float> w) const;
        juce::Point<float> toWorld (juce::Point<float> s) const;
        juce::Rectangle<float> toScreen (juce::Rectangle<float> w) const;
        void computeLayout();
        void updateScrollBars();
        void storeView();
        juce::Rectangle<float> contentBounds() const;
        const Node* nodeFor (int slot) const;
        Node* nodeFor (int slot);
        int nodeIndexAt (juce::Point<float> screen, float marginWorld) const;
        /** Presa di un oggetto (sullo schermo): ingresso (sinistra) o uscita (destra), linea A/B. */
        juce::Point<float> portPoint (const Node&, bool output, int line) const;
        bool endPoint (const routing::End&, juce::Point<float>& screen) const;
        juce::Rectangle<float> plugArea (int conn, bool destEnd) const;
        bool plugAt (juce::Point<float> screen, int& conn, bool& destEnd) const;
        juce::Rectangle<float> ledArea (const Node&) const;
        routing::Target targetAt (juce::Point<float> screen, juce::Rectangle<float>* area) const;
        bool dropPlug (int conn, bool destEnd, juce::Point<float> screen);
        void showNotice (const juce::String&, bool ok);
        juce::Image thumbnailFor (const engine::ModelDef*);
        float plugScale() const { return 0.34f * zoom; }
        void paintNode (juce::Graphics&, const Node&, const Theme&);
        void paintCables (juce::Graphics&, const Theme&);
        void beginDrag (juce::Point<float> screen, const juce::ModifierKeys&);
        void continueDrag (juce::Point<float> screen, juce::Point<float> start);
        void endDrag (juce::Point<float> screen, bool moved);

        engine::Chain& chain;
        RouteLayout& layoutStore;
        juce::SharedResourcePointer<ThemeManager> themes;

        std::vector<Node> nodes;
        std::vector<routing::Conn> conns;
        juce::Rectangle<int> canvas;
        float zoom = 1.0f;
        juce::Point<float> origin;                  // angolo in alto a sinistra della vista (coordinate della mappa)
        int columns = 6;
        bool needFit = false;

        Drag drag = Drag::None;
        int dragConn = -1, dragNode = -1;
        bool dragDest = false;
        juce::Point<float> grabOffset, mouse, panStart;
        routing::Replug hover;
        juce::Rectangle<float> hoverArea;
        bool hoverValid = false;
        int hoverNode = -1000;                      // oggetto sotto il mouse (-1000 = nessuno; -1/-2 sono INPUT/OUTPUT)

        struct Notice { juce::String text; bool ok = true; juce::uint32 until = 0; } toast;
        juce::String stateKey;                       // LED / cavi: ridisegno se cambiano (anche da MIDI)

        std::map<juce::String, juce::Image> thumbs;
        juce::TextButton closeButton { "Chiudi" }, arrangeButton { "Disposizione automatica" }, fitButton { "Adatta" },
                         zoomOutButton { "-" }, zoomInButton { "+" };
        juce::Label title, help, zoomLabel;
        juce::ScrollBar hbar { false }, vbar { true };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoutingMap)
    };
}
