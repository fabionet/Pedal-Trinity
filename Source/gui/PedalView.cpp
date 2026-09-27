/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "PedalView.h"

namespace pt::ui
{
    using namespace pt::engine;

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
            const bool on = s->enabled.load();
            // LED
            if (on && d->ledR > 0)
            {
                const juce::Point<float> c (d->ledX, d->ledY);
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
        PedalView& owner;
    };

    //==============================================================================
    PedalView::PedalView (Chain& c, int slotIndex, juce::Component* popup) : chain (c), popupParent (popup)
    {
        bindTo (slotIndex);
        startTimerHz (15);
    }

    PedalView::~PedalView() { stopTimer(); }

    void PedalView::bindTo (int slotIndex)
    {
        index = slotIndex;
        auto* s = chain.slot (index);
        const auto* d = s != nullptr ? s->def : nullptr;
        if (s == slot && d == def && canvas != nullptr) return;
        slot = s;
        def = d;
        rebuild();
    }

    void PedalView::rebuild()
    {
        canvas.reset();
        source = {};
        scaled = {};
        scaledFor = {};
        if (def == nullptr || slot == nullptr || slot->fx == nullptr) { repaint(); return; }

        source = Assets::pedalImage (def->image);
        canvas = std::make_unique<Canvas> (*this);
        canvas->setSize ((int) std::ceil (def->imageW), (int) std::ceil (def->imageH));
        addAndMakeVisible (*canvas);

        const int idx = index;
        auto& ch = chain;
        auto bind = [this, idx, &ch] (BoundControl& b, int k)
        {
            b.onChange = [idx, k, &ch] (float v) { ch.setParam (idx, k, v); };
            b.readModel = [this, k] { return slot != nullptr && slot->fx != nullptr ? slot->fx->p (k) : 0.0f; };
            b.syncFromModel();
        };

        // pedale / footswitch
        auto* foot = new FootZone (def->footX, def->footY);
        if (def->family == Family::Looper)
        {
            foot->setTooltip ("REC / PLAY / DUB");
            foot->onClick = [this] { if (slot != nullptr && slot->fx != nullptr) slot->fx->trigger (0); };
        }
        else
            foot->onClick = [this, idx] { if (slot != nullptr) chain.setEnabled (idx, ! slot->enabled.load()); };
        canvas->addAndMakeVisible (foot);

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
                        auto* kn = new KnobControl (*assets, c);
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

    juce::Rectangle<float> PedalView::imageArea() const
    {
        if (def == nullptr) return getLocalBounds().toFloat();
        const float s = juce::jmin ((float) getWidth() / def->imageW, (float) getHeight() / def->imageH);
        return juce::Rectangle<float> (def->imageW * s, def->imageH * s).withCentre (getLocalBounds().toFloat().getCentre());
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
        if (def == nullptr || ! source.isValid())
        {
            // slot vuoto
            auto r = getLocalBounds().toFloat().reduced (juce::jmin (getWidth(), getHeight()) * 0.08f);
            g.setColour (juce::Colour (0x10ffffff));
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (juce::Colour (0x40ffffff));
            const float dash[] = { 6.0f, 5.0f };
            juce::Path p; p.addRoundedRectangle (r, 10.0f);
            juce::Path dashed; juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dash, 2);
            g.fillPath (dashed);
            g.setColour (juce::Colour (0x90ffffff));
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
        if (def != nullptr && (def->family == Family::Tuner || def->family == Family::Looper))
            canvas->repaint();
        const bool on = slot != nullptr && slot->enabled.load();
        if (on != lastOn) { lastOn = on; repaint(); canvas->repaint(); }
    }
}
