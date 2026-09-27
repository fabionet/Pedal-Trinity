/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
    Fabbriche delle famiglie DSP (implementate nei file Fx*.cpp).
*/

#pragma once

#include <memory>
#include "Effect.h"

namespace pt::engine
{
    std::unique_ptr<Effect> makeCircuit (const ModelDef&);
    std::unique_ptr<Effect> makeBbd (const ModelDef&);
    std::unique_ptr<Effect> makePhaser (const ModelDef&);
    std::unique_ptr<Effect> makeTremolo (const ModelDef&);
    std::unique_ptr<Effect> makeCompressor (const ModelDef&);
    std::unique_ptr<Effect> makeNoiseGate (const ModelDef&);
    std::unique_ptr<Effect> makeNam (const ModelDef&);
    std::unique_ptr<Effect> makeSlowGear (const ModelDef&);
    std::unique_ptr<Effect> makeVolume (const ModelDef&);
    std::unique_ptr<Effect> makeGraphicEQ (const ModelDef&);
    std::unique_ptr<Effect> makeParametricEQ (const ModelDef&);
    std::unique_ptr<Effect> makeWah (const ModelDef&);
    std::unique_ptr<Effect> makeAcoustic (const ModelDef&);
    std::unique_ptr<Effect> makeDigitalDelay (const ModelDef&);
    std::unique_ptr<Effect> makeTapeEcho (const ModelDef&);
    std::unique_ptr<Effect> makeReverb (const ModelDef&);
    std::unique_ptr<Effect> makePitch (const ModelDef&);
    std::unique_ptr<Effect> makeSynth (const ModelDef&);
    std::unique_ptr<Effect> makeTuner (const ModelDef&);
    std::unique_ptr<Effect> makeAmpSim (const ModelDef&);
    std::unique_ptr<Effect> makeCabIR (const ModelDef&);
    std::unique_ptr<Effect> makeRouter (const ModelDef&);
    std::unique_ptr<Effect> makeLooper (const ModelDef&);
}
