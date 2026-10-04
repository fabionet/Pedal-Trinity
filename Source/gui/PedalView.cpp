/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "PedalView.h"
#include "../engine/FxNam.h"

namespace pt::ui
{
    using namespace pt::engine;

    /** Footswitch metallico renderizzato (NAM-A1A2): zona circolare che accende/spegne un canale. */
    class FootToggle : public juce::Component, public BoundControl, public juce::SettableTooltipClient
    {
    public:
        /** showState: pulsanti e kickswitch dei wah a bilanciere (stato non mostrato da un LED dedicato):
            un anello luminoso sul tasto quando e' inserito. */
        explicit FootToggle (const ControlDef& cd, bool showStateRing = false) : c (cd), showState (showStateRing)
        {
            const float r = c.radius * (showState ? 1.6f : 1.15f);
            setBounds (juce::Rectangle<float> (c.x - r, c.y - r, r * 2.0f, r * 2.0f).getSmallestIntegerContainer());
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            if (showState && c.steps == 2 && c.choices != nullptr)
                setTooltip (juce::String (c.label) + ": " + juce::String (c.choices).replace ("|", " / "));
            else
                setTooltip (juce::String (c.label).length() <= 1 ? juce::String ("Canale ") + c.label + ": acceso / spento"
                                                                   : juce::String (c.label) + ": footswitch");
        }
        bool hitTest (int x, int y) override
        {
            const auto ctr = juce::Point<float> (c.x, c.y) - getPosition().toFloat();
            return ctr.getDistanceFrom ({ (float) x, (float) y }) <= c.radius * 1.15f;
        }
        void mouseDown (const juce::MouseEvent&) override { pressed = true; repaint(); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            pressed = false;
            if (hitTest (e.x, e.y)) { state = ! state; if (onChange) onChange (state ? 1.0f : 0.0f); }
            repaint();
        }
        void paint (juce::Graphics& g) override
        {
            const auto ctr = juce::Point<float> (c.x, c.y) - getPosition().toFloat();
            if (showState && state)
            {
                const float r = c.radius * 1.32f;
                juce::ColourGradient glow (juce::Colour (0x00ffb030), ctr, juce::Colour (0x00ffb030), ctr.translated (r * 1.2f, 0), true);
                glow.addColour (0.62, juce::Colour (0x00ffb030));
                glow.addColour (0.80, juce::Colour (0xaaffb030));
                g.setGradientFill (glow);
                g.fillEllipse (juce::Rectangle<float> (r * 2.4f, r * 2.4f).withCentre (ctr));
                g.setColour (juce::Colour (0xffffc050));
                g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (ctr), 1.4f);
            }
            if (! pressed) return;
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (c.radius * 2.0f, c.radius * 2.0f).withCentre (ctr));
        }
        void syncFromModel() override
        {
            const bool v = readModel && readModel() > 0.5f;
            if (v != state) { state = v; repaint(); }
        }
    private:
        const ControlDef& c;
        bool showState = false;
        bool state = true, pressed = false;
    };

    /** Superficie in coordinate logiche dell'immagine: contiene i comandi. */
    class PedalView::Canvas : public juce::Component
    {
    public:
        Canvas (PedalView& o) : owner (o) { setInterceptsMouseClicks (false, true); }
        ~Canvas() override { deleteAllChildren(); }     // il canvas possiede i comandi che contiene

        void paintOverChildren (juce::Graphics& g) override
        {
            auto* d = owner.def;
            auto* s = owner.slot;
            if (d == nullptr || s == nullptr) return;
            const bool on = s->enabled.load() && (s->patched.load() || s->isSplitter());   // staccato: LED spento
            if (d->family == Family::Nam) { paintNam (g, *d, *s, on); return; }
            // LED
            if (on && d->ledR > 0)
            {
                const auto c = owner.ledPoint();
                const float r = d->ledR;
                juce::ColourGradient halo (juce::Colour (0x88ff2a14), c, juce::Colour (0x00ff2a14), c.translated (r * 6.5f, 0), true);
                g.setGradientFill (halo);
                g.fillEllipse (juce::Rectangle<float> (r * 13.0f, r * 13.0f).withCentre (c));
                juce::ColourGradient core (juce::Colour (0xfffff0e0), c.translated (-r * 0.25f, -r * 0.3f), juce::Colour (0xffff2a10), c.translated (r, 0), true);
                g.setGradientFill (core);
                g.fillEllipse (juce::Rectangle<float> (r * 2.1f, r * 1.9f).withCentre (c));
            }
            if (s->fx == nullptr) return;
            // display dell'accordatore
            if (d->family == Family::Tuner && on)
            {
                static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                const float valid = s->fx->readout (3), cents = s->fx->readout (1);
                const int note = (int) s->fx->readout (2);
                auto area = d->dispW > 0 ? juce::Rectangle<float> (d->dispX, d->dispY, d->dispW, d->dispH)
                                         : juce::Rectangle<float> (d->imageW * 0.18f, d->imageH * 0.09f, d->imageW * 0.64f, d->imageH * 0.16f);
                g.setColour (juce::Colour (0xff050607));
                g.fillRoundedRectangle (area, 4.0f);
                const int leds = 21;
                const float lw = area.getWidth() / (float) leds;
                const int lit = juce::roundToInt ((juce::jlimit (-50.0f, 50.0f, cents) + 50.0f) / 100.0f * (leds - 1));
                for (int i = 0; i < leds; ++i)
                {
                    const bool centre = i == leds / 2;
                    const bool isLit = valid > 0.5f && (i == lit || (centre && std::abs (cents) < 3.0f));
                    auto col = centre ? juce::Colour (0xff30ff60) : juce::Colour (0xffff3020);
                    g.setColour (isLit ? col : col.withAlpha (0.15f));
                    g.fillRoundedRectangle (area.getX() + i * lw + 1, area.getY() + 4, lw - 2, area.getHeight() * 0.35f, 1.5f);
                }
                g.setColour (valid > 0.5f ? juce::Colour (0xffff5040) : juce::Colour (0x40ff5040));
                g.setFont (juce::Font (area.getHeight() * 0.5f, juce::Font::bold));
                g.drawText (valid > 0.5f ? juce::String (names[juce::jlimit (0, 11, note)]) : "-",
                            area.withTrimmedTop (area.getHeight() * 0.45f), juce::Justification::centred);
            }
            // stato del looper
            if (d->family == Family::Looper)
            {
                static const char* st[] = { "VUOTO", "REC", "PLAY", "DUB", "STOP" };
                const int state = juce::jlimit (0, 4, (int) s->fx->readout (0));
                const float pos = s->fx->readout (1), len = s->fx->readout (2);
                auto area = d->dispW > 0 ? juce::Rectangle<float> (d->dispX, d->dispY, d->dispW, d->dispH)
                                         : juce::Rectangle<float> (d->imageW * 0.22f, d->imageH * 0.08f, d->imageW * 0.56f, d->imageH * 0.1f);
                g.setColour (juce::Colour (0xff050607));
                g.fillRoundedRectangle (area, 3.0f);
                const juce::Colour col = state == 1 ? juce::Colour (0xffff3030) : state == 3 ? juce::Colour (0xffffa020) : juce::Colour (0xff40ff70);
                g.setColour (col.withAlpha (0.25f));
                g.fillRoundedRectangle (area.withWidth (area.getWidth() * pos), 3.0f);
                g.setColour (col);
                g.setFont (juce::Font (area.getHeight() * 0.55f, juce::Font::bold));
                g.drawText (juce::String (st[state]) + "  " + juce::String (len, 1) + " s", area, juce::Justification::centred);
            }
        }
        /** NAM-A1A2: LED dei due canali e display con i meter a tacche LED (IN, NAM, IR per A e B). */
        void paintNam (juce::Graphics& g, const ModelDef& d, Slot& s, bool on)
        {
            for (int k = 0; k < d.numControls; ++k)
            {
                const auto& c = d.controls[k];
                if (c.kind != ControlKind::Toggle || c.strip != 255 || s.fx == nullptr) continue;
                if (! on || s.fx->p (k) < 0.5f) continue;
                const juce::Point<float> p (c.tx, c.ty);
                const float r = d.ledR > 0 ? d.ledR : 8.0f;
                juce::ColourGradient halo (juce::Colour (0x88ff2a14), p, juce::Colour (0x00ff2a14), p.translated (r * 6.0f, 0), true);
                g.setGradientFill (halo);
                g.fillEllipse (juce::Rectangle<float> (r * 12.0f, r * 12.0f).withCentre (p));
                juce::ColourGradient core (juce::Colour (0xfffff0e0), p.translated (-r * 0.25f, -r * 0.3f), juce::Colour (0xffff2a10), p.translated (r, 0), true);
                g.setGradientFill (core);
                g.fillEllipse (juce::Rectangle<float> (r * 2.1f, r * 1.9f).withCentre (p));
            }
            if (d.dispW <= 0) return;
            auto area = juce::Rectangle<float> (d.dispX, d.dispY, d.dispW, d.dispH).reduced (d.dispW * 0.035f, d.dispH * 0.08f);
            auto* nam = dynamic_cast<NamEffect*> (s.fx.get());
            const char* names[3] = { "IN", "NAM", "IR" };
            const float rowH = area.getHeight() / 2.0f;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto row = area.withHeight (rowH).translated (0, rowH * (float) ch);
                const bool active = on && s.fx->p (ch == 0 ? NamEffect::ChanA : NamEffect::ChanB) > 0.5f;
                const auto st = nam != nullptr ? nam->status (ch) : NamEffect::ChannelStatus();
                g.setColour (active ? juce::Colour (0xffff5a3a) : juce::Colour (0x55ff5a3a));
                g.setFont (juce::Font (rowH * 0.55f, juce::Font::bold));
                g.drawText (ch == 0 ? "A" : "B", row.removeFromLeft (row.getWidth() * 0.07f), juce::Justification::centred);
                const float mw = row.getWidth() / 3.0f;
                const bool empty = nam != nullptr && ! st.hasNam && ! st.hasIr;
                for (int m = 0; m < (empty ? 1 : 3); ++m)
                {
                    auto cell = row.withWidth (mw).translated (mw * (float) m, 0).reduced (mw * 0.05f, rowH * 0.12f);
                    g.setColour (juce::Colour (0xff9a8a80));
                    g.setFont (juce::Font (cell.getHeight() * 0.32f, juce::Font::bold));
                    g.drawText (names[m], cell.removeFromTop (cell.getHeight() * 0.36f), juce::Justification::centredLeft);
                    // tacche LED orizzontali: -48 .. +6 dB
                    const int seg = 14;
                    const float lvl = owner.namLevels[(size_t) (ch * 3 + m)];
                    const float db = juce::Decibels::gainToDecibels (lvl, -100.0f);
                    const int lit = juce::jlimit (0, seg, (int) std::floor ((db + 48.0f) / 54.0f * (float) seg + 0.5f));
                    const float sw = cell.getWidth() / (float) seg;
                    for (int i = 0; i < seg; ++i)
                    {
                        const float segDb = -48.0f + 54.0f * (float) (i + 1) / (float) seg;
                        const auto col = segDb > -3.0f ? juce::Colour (0xffff3020) : segDb > -12.0f ? juce::Colour (0xffffc030) : juce::Colour (0xff40e070);
                        g.setColour (i < lit && active ? col : col.withAlpha (0.13f));
                        g.fillRoundedRectangle (cell.getX() + sw * (float) i + sw * 0.12f, cell.getY(), sw * 0.76f, cell.getHeight() * 0.8f, sw * 0.15f);
                    }
                }
                // al posto dei meter NAM/IR: canale vuoto o file rifiutato dalla verifica di sicurezza
                if (empty)
                {
                    const auto msgArea = row.withTrimmedLeft (mw).reduced (mw * 0.05f, rowH * 0.12f);
                    g.setColour (st.warning ? juce::Colour (0xffff6a4a) : juce::Colour (0x77ffffff));
                    g.setFont (juce::Font (rowH * 0.32f, st.warning ? juce::Font::bold : juce::Font::plain));
                    g.drawFittedText (st.warning ? "! file rifiutato: vedi lo zoom" : "vuoto: carica i file dallo zoom",
                                      msgArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
                }
                else if (st.warning)
                {
                    g.setColour (juce::Colour (0xffff6a4a));
                    g.fillEllipse (juce::Rectangle<float> (rowH * 0.18f, rowH * 0.18f).withCentre (row.getTopRight().translated (-rowH * 0.12f, rowH * 0.14f)));
                }
            }
        }

        PedalView& owner;
    };

    //==============================================================================
    //==============================================================================
    /** Sovrapposizione di allineamento: cerchi trascinabili sui pomelli della foto (rotellina = dimensione)
        e un mirino per il LED CHECK. */
    class PedalView::Aligner : public juce::Component
    {
    public:
        Aligner (PedalView& o) : owner (o)
        {
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            const auto* d = owner.def;
            for (int k = 0; k < d->numControls; ++k)
            {
                const auto& c = d->controls[k];
                if (c.kind != ControlKind::Knob && c.kind != ControlKind::KnobOuter && c.kind != ControlKind::Selector) continue;
                AlignItem it;
                it.control = k;
                it.label = c.label;
                it.centre = { (c.x + c.tx) * 0.5f, (c.y + c.ty) * 0.5f };
                it.radius = c.radius;
                for (const auto& p : owner.photoControls)
                    if (p.first.equalsIgnoreCase (c.label))
                    {
                        it.centre = { d->bodyX + p.second.x * d->bodyW, d->bodyY + p.second.y * d->bodyH };
                        it.radius = p.second.z * d->bodyW;
                    }
                items.push_back (it);
            }
            led = owner.ledPoint();
        }
        std::vector<AlignItem> items;
        juce::Point<float> led;

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::black.withAlpha (0.12f));
            for (int i = 0; i < (int) items.size(); ++i)
            {
                const auto& it = items[(size_t) i];
                const bool sel = i == selected;
                const auto r = juce::Rectangle<float> (it.radius * 2, it.radius * 2).withCentre (it.centre);
                g.setColour ((sel ? juce::Colour (0xffffd040) : juce::Colour (0xff40e0ff)).withAlpha (0.95f));
                const float dash[] = { 5.0f, 3.0f };
                juce::Path circle; circle.addEllipse (r);
                juce::Path dashed; juce::PathStrokeType (sel ? 2.4f : 1.6f).createDashedStroke (dashed, circle, dash, 2);
                g.fillPath (dashed);
                g.drawLine (it.centre.x - 4, it.centre.y, it.centre.x + 4, it.centre.y, 1.2f);
                g.drawLine (it.centre.x, it.centre.y - 4, it.centre.x, it.centre.y + 4, 1.2f);
                g.setFont (juce::Font (11.0f, juce::Font::bold));
                const auto tr = juce::Rectangle<float> (80, 14).withCentre ({ it.centre.x, r.getBottom() + 9 });
                g.setColour (juce::Colours::black.withAlpha (0.7f));
                g.fillRoundedRectangle (tr.reduced (14, 0), 3.0f);
                g.setColour (juce::Colours::white);
                g.drawText (it.label, tr, juce::Justification::centred);
            }
            g.setColour (selected == ledIndex ? juce::Colour (0xffffd040) : juce::Colour (0xffff4040));
            g.drawEllipse (juce::Rectangle<float> (14, 14).withCentre (led), 1.8f);
            g.drawLine (led.x - 10, led.y, led.x + 10, led.y, 1.2f);
            g.drawLine (led.x, led.y - 10, led.x, led.y + 10, 1.2f);
        }
        int pick (juce::Point<float> p) const
        {
            if (p.getDistanceFrom (led) < 9) return ledIndex;
            int best = -1; float bd = 1e9f;
            for (int i = 0; i < (int) items.size(); ++i)
            {
                const float d = p.getDistanceFrom (items[(size_t) i].centre);
                if (d < items[(size_t) i].radius * 1.2f && d < bd) { bd = d; best = i; }
            }
            return best;
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            selected = pick (e.position);
            grab = e.position - (selected == ledIndex ? led : selected >= 0 ? items[(size_t) selected].centre : e.position);
            repaint();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            const auto p = (e.position - grab).withX (juce::jlimit (0.0f, (float) getWidth(), e.position.x - grab.x))
                                                .withY (juce::jlimit (0.0f, (float) getHeight(), e.position.y - grab.y));
            if (selected == ledIndex) { led = p; owner.photoLed = true; owner.photoLedAt = p; owner.canvasRepaint(); }
            else if (selected >= 0) { items[(size_t) selected].centre = p; owner.applyAlignItem (items[(size_t) selected]); }
            repaint();
        }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            const int i = selected >= 0 && selected != ledIndex ? selected : pick (e.position);
            if (i < 0 || i == ledIndex) return;
            auto& it = items[(size_t) i];
            it.radius = juce::jlimit (4.0f, 200.0f, it.radius * (w.deltaY > 0 ? 1.04f : 1.0f / 1.04f));
            selected = i;
            owner.applyAlignItem (it);
            repaint();
        }
        static constexpr int ledIndex = 1000;
    private:
        PedalView& owner;
        int selected = -1;
        juce::Point<float> grab;
    };

    PedalView::PedalView (Chain& c, int slotIndex, juce::Component* popup) : chain (c), popupParent (popup)
    {
        // i clic sul corpo del pedale (fuori dai comandi) arrivano allo slot: trascinamento a mano
        setInterceptsMouseClicks (false, true);
        bindTo (slotIndex);
        startTimerHz (15);
    }

    PedalView::~PedalView() { stopTimer(); }

    void PedalView::bindTo (int slotIndex)
    {
        index = slotIndex;
        auto ref = chain.slotRef (index);
        const auto* d = ref != nullptr ? ref->def : nullptr;
        const auto* v = visualDef (d);
        RealPhotos::Photo photo;
        if (v != d && v != nullptr) photo = photos->forModel (*v);
        if (ref.get() == slot && d == engineDef && v == def && photo.image == photoImage && (canvas != nullptr || d == nullptr)) return;
        slotRef = ref;
        slot = ref.get();
        engineDef = d;
        def = v;
        photoImage = photo.image;
        photoKnobs = photo.knobs;
        photoControls = photo.controls;
        photoFile = photo.file;
        photoLed = photo.hasLed && v != nullptr && v->bodyW > 0;
        if (photoLed) photoLedAt = { v->bodyX + photo.ledU * v->bodyW, v->bodyY + photo.ledV * v->bodyH };
        rebuild();
    }

    void PedalView::rebuild()
    {
        aligner.reset();
        knobComps.clear();
        canvas.reset();
        source = {};
        scaled = {};
        scaledFor = {};
        if (def == nullptr || slot == nullptr || slot->fx == nullptr) { repaint(); return; }

        source = photoImage;                                 // foto personale in rilievo (REAL MOD)
        if (! source.isValid())
        {
            source = Assets::pedalImage (def->image);
            photoKnobs = true;
            photoControls.clear();
            photoLed = false;
        }
        canvas = std::make_unique<Canvas> (*this);
        canvas->setSize ((int) std::ceil (def->imageW), (int) std::ceil (def->imageH));
        addAndMakeVisible (*canvas);

        const int idx = index;
        auto& ch = chain;
        auto bind = [this, idx, &ch] (BoundControl& b, int k)
        {
            b.onChange = [idx, k, &ch] (float v) { ch.setParam (idx, k, v); ch.notifyTouch (idx, k); };
            b.readModel = [this, k] { return slot != nullptr && slot->fx != nullptr ? slot->fx->p (k) : 0.0f; };
            b.syncFromModel();
        };

        // pedale / footswitch (il NAM-A1A2 ha due footswitch di canale al posto del pedale)
        auto* foot = def->family == Family::Nam ? nullptr : new FootZone (def->footX, def->footY);
        if (foot != nullptr) {
        if (def->family == Family::Looper)
        {
            foot->setTooltip ("REC / PLAY / DUB");
            foot->onClick = [this] { if (slot != nullptr && slot->fx != nullptr) slot->fx->trigger (0); };
        }
        else
            foot->onClick = [this, idx] { if (slot != nullptr) { chain.setEnabled (idx, ! slot->enabled.load()); chain.notifyTouch (idx, -1); } };
        canvas->addAndMakeVisible (foot);
        }

        // prima gli anelli esterni, poi i pomelli interni (sopra)
        for (int pass = 0; pass < 2; ++pass)
            for (int k = 0; k < def->numControls; ++k)
            {
                const auto& c = def->controls[k];
                const bool inner = c.kind == ControlKind::KnobInner;
                if ((pass == 1) != inner) continue;
                juce::Component* comp = nullptr;
                switch (c.kind)
                {
                    case ControlKind::Knob: case ControlKind::KnobOuter: case ControlKind::KnobInner: case ControlKind::Selector:
                    {
                        if (c.strip == 254)          // bilanciere (REAL MOD: volume, espressione, wah)
                        {
                            auto* t = new TreadleControl (c);
                            t->setPopupDisplayEnabled (true, false, popupParent);
                            bind (*t, k);
                            comp = t;
                            break;
                        }
                        auto* kn = new KnobControl (*assets, c);
                        kn->setGhost (! photoKnobs);
                        knobComps[k] = kn;
                        if (photoKnobs && ! photoControls.empty())
                            placeOnPhoto (*kn, k);           // pomello 3D esattamente sopra quello della foto
                        if (c.kind == ControlKind::KnobOuter)
                            for (int j = 0; j < def->numControls; ++j)
                                if (def->controls[j].kind == ControlKind::KnobInner && std::abs (def->controls[j].x - c.x) < 1.0f)
                                    kn->setExclusion (def->controls[j].radius * 1.05f);
                        kn->setPopupDisplayEnabled (true, false, popupParent);
                        bind (*kn, k);
                        comp = kn;
                        break;
                    }
                    case ControlKind::Slider:
                    {
                        auto* f = new FaderControl (*assets, c);
                        f->setPopupDisplayEnabled (true, false, popupParent);
                        bind (*f, k);
                        comp = f;
                        break;
                    }
                    case ControlKind::Toggle:
                    {
                        if (c.strip == 255)          // footswitch renderizzato (NAM-A1A2)
                        {
                            auto* f = new FootToggle (c, def->family != Family::Nam);
                            bind (*f, k);
                            comp = f;
                            break;
                        }
                        auto* t = new ToggleControl (*assets, c);
                        bind (*t, k);
                        comp = t;
                        break;
                    }
                    case ControlKind::Button:
                    {
                        auto* b = new PushControl (c);
                        const juce::String r (c.role != nullptr ? c.role : "");
                        const int action = r == "stop" ? 1 : r == "clear" ? 2 : r == "undo" ? 3 : 0;
                        b->onPress = [this, action] { if (slot != nullptr && slot->fx != nullptr) slot->fx->trigger (action); };
                        comp = b;
                        break;
                    }
                }
                if (comp != nullptr) canvas->addAndMakeVisible (comp);
            }
        resized();
        repaint();
    }

    juce::Point<float> PedalView::ledPoint() const
    {
        return photoLed ? photoLedAt : juce::Point<float> (def->ledX, def->ledY);
    }

    void PedalView::placeOnPhoto (KnobControl& kn, int k)
    {
        const auto& c = def->controls[k];
        auto find = [this] (const char* label) -> const PhotoKnob*
        {
            for (const auto& p : photoControls)
                if (p.first.equalsIgnoreCase (label)) return &p.second;
            return nullptr;
        };
        const auto* p = find (c.label);
        float rScale = 1.0f;
        if (p == nullptr && c.kind == ControlKind::KnobInner)
            for (int j = 0; j < def->numControls && p == nullptr; ++j)
                if (def->controls[j].kind == ControlKind::KnobOuter && std::abs (def->controls[j].x - c.x) < 1.0f)
                    if ((p = find (def->controls[j].label)) != nullptr) rScale = def->controls[k].radius / juce::jmax (1.0f, def->controls[j].radius);
        if (p == nullptr || def->bodyW <= 0.0f)
        {
            kn.setGhost (true);                        // pomello non presente nella foto: solo l'arco del valore
            return;
        }
        // centro e raggio del pomello della foto, nelle coordinate dell'immagine della replica
        kn.setGhost (false);
        const juce::Point<float> target (def->bodyX + p->x * def->bodyW, def->bodyY + p->y * def->bodyH);
        const float radius = p->z * def->bodyW * rScale;
        const juce::Point<float> centre ((c.x + c.tx) * 0.5f, (c.y + c.ty) * 0.5f);
        const float s = juce::jlimit (0.3f, 4.0f, radius / juce::jmax (1.0f, c.radius));
        kn.setTransform (juce::AffineTransform::translation (target - centre).scaled (s, s, target.x, target.y));
    }

    void PedalView::setKnobPlacement (int k, juce::Point<float> target, float radius)
    {
        const auto it = knobComps.find (k);
        if (it == knobComps.end()) return;
        const auto& c = def->controls[k];
        const juce::Point<float> centre ((c.x + c.tx) * 0.5f, (c.y + c.ty) * 0.5f);
        const float s = juce::jlimit (0.3f, 4.0f, radius / juce::jmax (1.0f, c.radius));
        it->second->setGhost (false);
        it->second->setTransform (juce::AffineTransform::translation (target - centre).scaled (s, s, target.x, target.y));
        // l'anello interno di un pomello concentrico segue il suo esterno
        if (c.kind == ControlKind::KnobOuter)
            for (int j = 0; j < def->numControls; ++j)
                if (def->controls[j].kind == ControlKind::KnobInner && std::abs (def->controls[j].x - c.x) < 1.0f)
                    setKnobPlacement (j, target, radius * def->controls[j].radius / juce::jmax (1.0f, c.radius));
    }

    void PedalView::canvasRepaint() { if (canvas != nullptr) canvas->repaint(); }

    void PedalView::applyAlignItem (const AlignItem& it) { setKnobPlacement (it.control, it.centre, it.radius); }

    void PedalView::setAlignMode (bool on)
    {
        if (on == isAligning()) return;
        if (! on) { aligner.reset(); rebuild(); return; }        // annulla: torna alle posizioni salvate
        if (! hasPhoto() || canvas == nullptr) return;
        aligner = std::make_unique<Aligner> (*this);
        aligner->setBounds (canvas->getLocalBounds());
        canvas->addAndMakeVisible (*aligner);
        aligner->toFront (false);
        for (const auto& it : aligner->items) applyAlignItem (it);
    }

    juce::String PedalView::saveAlignment()
    {
        if (aligner == nullptr || def == nullptr || def->bodyW <= 0) return "nessun allineamento in corso";
        std::vector<std::pair<juce::String, PhotoKnob>> out;
        for (const auto& it : aligner->items)
            out.push_back ({ it.label, PhotoKnob { (it.centre.x - def->bodyX) / def->bodyW, (it.centre.y - def->bodyY) / def->bodyH,
                                                   it.radius / def->bodyW } });
        const auto l = aligner->led;
        const auto err = RealPhotos::saveAlignment (photoFile, out, true, (l.x - def->bodyX) / def->bodyW, (l.y - def->bodyY) / def->bodyH);
        if (err.isNotEmpty()) return err;
        aligner.reset();
        photos->reload();
        slot = nullptr;                                          // forza la ricostruzione con le nuove posizioni
        bindTo (index);
        return {};
    }

    juce::Rectangle<float> PedalView::imageArea() const
    {
        if (def == nullptr) return getLocalBounds().toFloat();
        const float s = juce::jmin ((float) getWidth() / def->imageW, (float) getHeight() / def->imageH);
        return juce::Rectangle<float> (def->imageW * s, def->imageH * s).withCentre (getLocalBounds().toFloat().getCentre());
    }

    float PedalView::imageScale() const
    {
        return def != nullptr ? imageArea().getWidth() / def->imageW : 1.0f;
    }

    bool PedalView::jackPoint (bool output, int line, juce::Point<float>& out) const
    {
        if (def == nullptr || slot == nullptr || slot->fx == nullptr) return false;
        const auto a = imageArea();
        const float s = a.getWidth() / def->imageW;
        const float x = output ? def->jackOutX : def->jackInX;
        const float y = line == 1 ? def->jackYB : def->jackYA;
        out = { a.getX() + x * s, a.getY() + y * s };
        return true;
    }

    void PedalView::resized()
    {
        if (canvas == nullptr || def == nullptr) return;
        const auto a = imageArea();
        const float s = a.getWidth() / def->imageW;
        canvas->setTransform (juce::AffineTransform::scale (s).translated (a.getX(), a.getY()));
    }

    void PedalView::paint (juce::Graphics& g)
    {
        if (def != nullptr && ! source.isValid())
        {
            // modello senza immagine: segnaposto nel colore del pedale
            const auto a = imageArea().reduced (4.0f);
            const auto body = juce::Colour (def->colour).withAlpha (1.0f);
            g.setGradientFill (juce::ColourGradient (body.brighter (0.25f), a.getTopLeft(), body.darker (0.45f), a.getBottomLeft(), false));
            g.fillRoundedRectangle (a, a.getWidth() * 0.05f);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawRoundedRectangle (a, a.getWidth() * 0.05f, 2.0f);
            g.setColour (body.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff161618) : juce::Colour (0xffeeecde));
            const auto t = a.withTrimmedTop (a.getHeight() * 0.72f).reduced (a.getWidth() * 0.06f, 0.0f);
            g.setFont (juce::Font (juce::jlimit (10.0f, 22.0f, a.getWidth() * 0.075f), juce::Font::bold | juce::Font::italic));
            g.drawFittedText (juce::String (def->name) + "\n" + def->code, t.toNearestInt(), juce::Justification::centredTop, 2);
            return;
        }
        if (def == nullptr || ! source.isValid())
        {
            // slot vuoto
            auto r = getLocalBounds().toFloat().reduced (juce::jmin (getWidth(), getHeight()) * 0.08f);
            const auto& t = themes->current();
            const auto ink = themes->boardTheme().ink;      // segni leggibili sulla pedana scelta
            g.setColour (ink.withAlpha (0.07f));
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (ink.withAlpha (0.55f));
            const float dash[] = { 6.0f, 5.0f };
            juce::Path p; p.addRoundedRectangle (r, 10.0f);
            juce::Path dashed; juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dash, 2);
            g.fillPath (dashed);
            g.setColour (t.header.withAlpha (0.85f));
            g.fillRoundedRectangle (r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 12.0f, 190.0f), 70.0f), 8.0f);
            g.setColour (t.text);
            g.setFont (juce::Font (juce::jlimit (11.0f, 18.0f, r.getWidth() * 0.09f), juce::Font::bold));
            g.drawFittedText ("Slot vuoto\nscegli un pedale\ndal menu", r.toNearestInt(), juce::Justification::centred, 3);
            return;
        }
        // immagine riscalata una volta per dimensione (alta qualita'), poi disegnata 1:1
        const auto a = imageArea();
        const float scale = juce::Component::getApproximateScaleFactorForComponent (this);
        const auto phys = (a * scale).getSmallestIntegerContainer();
        if (phys.getWidth() > 0 && (scaledFor.getWidth() != phys.getWidth() || scaledFor.getHeight() != phys.getHeight()))
        {
            scaled = source.rescaled (phys.getWidth(), phys.getHeight(), juce::Graphics::highResamplingQuality);
            scaledFor = phys;
        }
        g.drawImage (scaled, a, juce::RectanglePlacement::stretchToFit);
        if (slot != nullptr && ! slot->enabled.load())
        {
            g.setColour (juce::Colours::black.withAlpha (0.18f));    // pedale spento: leggermente scurito
            g.fillRect (a);
        }
    }

    void PedalView::timerCallback()
    {
        if (canvas == nullptr) return;
        for (auto* c : canvas->getChildren())
            if (auto* b = dynamic_cast<BoundControl*> (c))
                b->syncFromModel();
        if (def != nullptr && def->family == Family::Nam && slot != nullptr && slot->fx != nullptr)
            for (int k = 0; k < NamEffect::numMeters; ++k)
            {
                // picco con discesa morbida (circa 20 dB al secondo)
                const float v = slot->fx->readout (k);
                namLevels[(size_t) k] = juce::jmax (v, namLevels[(size_t) k] * 0.72f);
            }
        if (def != nullptr && (def->family == Family::Tuner || def->family == Family::Looper || def->family == Family::Nam))
            canvas->repaint();
        const bool on = slot != nullptr && slot->enabled.load() && (slot->patched.load() || slot->isSplitter());
        if (on != lastOn) { lastOn = on; repaint(); canvas->repaint(); }
    }
}
