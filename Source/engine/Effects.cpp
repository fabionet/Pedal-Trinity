/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "Families.h"
#include "Circuit.h"

namespace pt::engine
{
    std::unique_ptr<Effect> makeCircuit (const ModelDef& d) { return std::make_unique<CircuitEffect> (d); }

    std::unique_ptr<Effect> createEffect (const ModelDef& d)
    {
        switch (d.family)
        {
            case Family::Circuit:      return makeCircuit (d);
            case Family::Compressor:   return makeCompressor (d);
            case Family::NoiseGate:    return makeNoiseGate (d);
            case Family::GraphicEQ:    return makeGraphicEQ (d);
            case Family::ParametricEQ: return makeParametricEQ (d);
            case Family::Wah:          return makeWah (d);
            case Family::BBDChorus:
            case Family::BBDFlanger:
            case Family::AnalogDelay:  return makeBbd (d);
            case Family::Phaser:       return makePhaser (d);
            case Family::Tremolo:      return makeTremolo (d);
            case Family::DigitalDelay: return makeDigitalDelay (d);
            case Family::TapeEcho:     return makeTapeEcho (d);
            case Family::Reverb:       return makeReverb (d);
            case Family::Pitch:        return makePitch (d);
            case Family::Synth:        return makeSynth (d);
            case Family::Acoustic:     return makeAcoustic (d);
            case Family::AmpSim:       return makeCircuit (d);      // gli amp sono netlist con triodi
            case Family::CabIR:        return makeCabIR (d);
            case Family::Router:       return makeRouter (d);
            case Family::Volume:       return makeVolume (d);
            case Family::Tuner:        return makeTuner (d);
            case Family::Looper:       return makeLooper (d);
            case Family::SlowGear:     return makeSlowGear (d);
            case Family::Count:        break;
        }
        return makeVolume (d);
    }
}
