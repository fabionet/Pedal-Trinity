/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Tappa 6: simulatori di cassa ("type=i6cab", categoria "Casse"). La risposta all'impulso si GENERA (nessuna IR
    copiata) con lo stesso metodo di generateCabIR (FxStage5Time.cpp), esteso:
      - percorsi del suono dal cono al microfono (FxStage6CabData.h: misure del mobile e posizione dei coni): ogni
        cono e' un pistone di raggio a (ampiezza a^2 / 2R, R = distanza con un termine di vicinanza a/2), ritardo di
        percorso, direttivita' (passa-basso a 1,6 c / (2 pi a sin theta)) e diagramma polare del microfono
        (1 - alfa + alfa cos phi) per i coni non puntati; onda posteriore dei mobili aperti attorno al fianco (in
        opposizione, passa-basso c / 2 (D + e)), diffrazione dai bordi del baffle, pavimento, pareti e coda diffusa
        della stanza (ROOM);
      - prossimita' calcolata per ogni percorso (1 + alfa / jkR, primo ordine con tetto a +20 dB) sopra la curva
        del microfono in campo lontano ("filters_farfield" della ricerca): la prossimita' non si somma due volte;
      - mobile: passa-alto e risonanza del sistema (engine_hints delle casse, scalati con la fs del cono), porta
        reflex (buco del cono a fb vicino, riempito lontano), modi assiali dell'aria interna e risonanza del
        pannello (formula della piastra con il materiale);
      - voce dell'altoparlante: 5 campane + passa-basso del cono adattate ai punti pubblicati (relativi a 1 kHz),
        con i picchi di presenza sopra 1,2 kHz scalati da kPresence (vedi sotto);
      - posizioni sul cono: differenze rispetto al centro (delta_points della ricerca, adattati in campane).
    Il calcolo avviene in prepare() (sincrono: uscita deterministica dal primo campione) e poi in un thread di
    lavoro quando cambiano i selettori o i pomelli che toccano l'IR; il thread audio passa alla nuova IR con una
    dissolvenza di 30 ms. La convoluzione e' partizionata uniforme a latenza zero (testa di B prese nel dominio
    del tempo + partizioni FFT da B), con la linea di ritardo spettrale dell'ingresso CONDIVISA fra la IR vecchia
    e la nuova: anche la nuova esce "calda" (gia' con la coda dei campioni passati). Nessuna allocazione nel
    thread audio. IR dell'utente (.wav/.aif, fino a 0,5 s, ricampionata e normalizzata come quelle generate).
    Ruoli: speaker mic position dist room mic2 blend m2dist m2pos locut hicut level (+ format bank nel laboratorio).
    Config: cab=<id>|lab spk=<id,...>[|<id,...>...] pos=<id,...> m2=back|room.
*/

#include "FxStage6.h"
#include "FxStage6CabData.h"
#include "NamSecurity.h"

#include <thread>
#include <mutex>
#include <condition_variable>

namespace pt::engine
{
    namespace
    {
        using namespace cx;

        constexpr double kSound = 343.0;                               // velocita' del suono (m/s)
        constexpr double kPiD = juce::MathConstants<double>::pi;

        /** Calibrazione della voce (report.md, "Calibrazione"): le curve dei datasheet (1 m in asse su baffle) hanno picchi
            di presenza di +5...+13 dB; letterali, cono + microfono vicino (es. V30 + SM57 al centro) danno fra 2 e 5 kHz
            ~9,6 dB in piu' (rispetto a 200 Hz - 1,5 kHz) delle macro PT_* tarate a orecchio con lo stesso microfono.
            Due correzioni globali uguali per tutti i coni (le differenze fra i coni restano quelle pubblicate):
              - i dB delle campane positive sopra 1,2 kHz x kPresence;
              - mensola alta del cono a kVoiceShelfHz (vicino al cono invece che a 1 m, taratura d'insieme).
            Risultato: +4,8 dB sulle macro al centro della cupola, +2,8 dB sul bordo della cupola (posizione di fabbrica). */
        constexpr double kPresence = 0.75;
        constexpr double kVoiceShelfHz = 2600.0, kVoiceShelfDb = -5.0;

        //==============================================================================
        /** Biquad in doppia precisione (stesse formule di BqD in FxStage5Time.cpp), per generare le IR. */
        struct BqD
        {
            double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
            double tick (double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
            void norm (double B0, double B1, double B2, double A0, double A1, double A2) noexcept
            { b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0; z1 = z2 = 0; }
            static double w (double sr, double f) noexcept { return 2.0 * kPiD * std::clamp (f, 5.0, 0.46 * sr) / sr; }
            void lp (double sr, double f, double q) noexcept { const double c = std::cos (w (sr, f)), al = std::sin (w (sr, f)) / (2 * q); norm ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); }
            void hp (double sr, double f, double q) noexcept { const double c = std::cos (w (sr, f)), al = std::sin (w (sr, f)) / (2 * q); norm ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); }
            void peak (double sr, double f, double q, double db) noexcept
            {
                const double A = std::pow (10.0, db / 40.0), c = std::cos (w (sr, f)), al = std::sin (w (sr, f)) / (2 * q);
                norm (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
            }
            void shelf (double sr, double f, double db, bool high) noexcept
            {
                const double A = std::pow (10.0, db / 40.0), c = std::cos (w (sr, f)), s = std::sin (w (sr, f));
                const double al = s / 2 * std::sqrt (2.0), k = 2 * std::sqrt (A) * al;
                if (high) norm (A * ((A + 1) + (A - 1) * c + k), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - k),
                                (A + 1) - (A - 1) * c + k, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - k);
                else norm (A * ((A + 1) - (A - 1) * c + k), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - k),
                           (A + 1) + (A - 1) * c + k, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - k);
            }
            /** Primo ordine (b2 = a2 = 0): passa-basso a un polo con pre-distorsione. */
            void lp1 (double sr, double f) noexcept
            {
                const double K = std::tan (kPiD * std::clamp (f, 5.0, 0.46 * sr) / sr);
                norm (K, K, 0, K + 1, K - 1, 0);
            }
            /** Prossimita' H(s) = (s + w0) / (s + w0 / G) (trasformata bilineare): +20 log G dB al limite in basso. */
            void prox (double sr, double w0, double G) noexcept
            {
                const double K = 2.0 * sr, w1 = w0 / G;
                norm (K + w0, w0 - K, 0, K + w1, w1 - K, 0);
            }
            void filt (double sr, const i6::Filt& f) noexcept
            {
                if (f.type == i6::FPeak) peak (sr, f.f, f.q, f.db);
                else shelf (sr, f.f, f.db, f.type == i6::FHigh);
            }
            void run (double* x, int from, int len) noexcept { for (int i = from; i < len; ++i) x[i] = tick (x[i]); }
        };

        //==============================================================================
        /** Tutto cio' che decide la IR (pomelli quantizzati: la IR si rigenera a passi). */
        struct IrKey
        {
            int cab = 0, spk = 0, mic1 = 0, pos1 = 0, mic2 = -1, m2mode = 0, user = 0;
            int dist = 32, room = 0, blend = 16, m2dist = 0;
            bool operator== (const IrKey& o) const noexcept
            {
                return cab == o.cab && spk == o.spk && mic1 == o.mic1 && pos1 == o.pos1 && mic2 == o.mic2 && m2mode == o.m2mode
                       && user == o.user && dist == o.dist && room == o.room && blend == o.blend && m2dist == o.m2dist;
            }
            bool operator!= (const IrKey& o) const noexcept { return ! (*this == o); }
        };
        constexpr int kDistSteps = 64, kRoomSteps = 32, kBlendSteps = 32, kM2Steps = 32;

        /** Un percorso dal cono (o da una sua immagine) al microfono. */
        struct Path
        {
            double t = 0, g = 0;         // ritardo (s) e ampiezza (polare del microfono compresa)
            double dirF = 0;             // passa-basso di direttivita' del cono (2o ordine), 0 = nessuno
            double lpF = 0;              // passa-basso a un polo (diffrazione, assorbimento), 0 = nessuno
            double proxR = 0;            // distanza per la prossimita' (m), 0 = nessuna
        };

        /** Un microfono piazzato. */
        struct MicPlace
        {
            const i6::Mic* mic = nullptr;
            const i6::Position* pos = nullptr;
            int kind = i6::OnCone;
            double d = 0.025;            // distanza dal piano del baffle (o dal retro) in m
            double lift = 0;             // spostamento verticale (secondo microfono accanto al primo)
            double weight = 1, polarity = 1, roomAmt = 0;
            bool useDelta = true;        // filtri di posizione (differenze rispetto al centro)
            uint32_t seed = 1;
        };

        //==============================================================================
        /** Generatore delle IR (thread di lavoro o prepare(): mai il thread audio). Memoria allocata in prepare(). */
        class CabGen
        {
        public:
            void prepare (double sampleRate, int maxLen)
            {
                sr = sampleRate;
                cap = maxLen;
                acc.assign ((size_t) cap, 0.0);
                xm.assign ((size_t) cap, 0.0);
                tmp.assign ((size_t) cap, 0.0);
                int order = 4;
                while ((1 << order) < cap) ++order;
                fft = std::make_unique<juce::dsp::FFT> (order);
                spec.assign ((size_t) (2 << order), 0.0f);
            }

            int capacity() const noexcept { return cap; }

            /** Genera la IR di 'k' in out[0..len) e restituisce len (deterministica). */
            int generate (const IrKey& k, const i6::Cab& cab, float* out)
            {
                const auto& spk = i6::kSpeakers[k.spk];
                a = 0.0106 * spk.sizeIn;                                   // raggio efficace (12" -> 12,7 cm, Sd ~ 507 cm^2)
                buildGeometry (cab);

                MicPlace m[2];
                const int nm = placeMics (k, m);
                double roomAny = 0;
                for (int i = 0; i < nm; ++i) roomAny = std::max (roomAny, m[i].roomAmt);
                const int len = std::min (cap, (int) std::lround ((roomAny > 1.0e-4 ? 0.25 : 0.07) * sr));
                std::fill (acc.begin(), acc.begin() + len, 0.0);

                // percorsi di tutti i microfoni, poi ritardo comune tolto (il primo arrivo cade sul campione 0)
                int np[2] = { 0, 0 };
                double tmin = 1.0e9;
                for (int i = 0; i < nm; ++i)
                {
                    np[i] = buildPaths (m[i], cab, paths[i]);
                    for (int j = 0; j < np[i]; ++j) tmin = std::min (tmin, paths[i][j].t);
                }
                double wfar = 0, wsum = 0;
                for (int i = 0; i < nm; ++i)
                {
                    std::fill (xm.begin(), xm.begin() + len, 0.0);
                    for (int j = 0; j < np[i]; ++j) renderPath (paths[i][j], tmin, *m[i].mic, len);
                    if (m[i].roomAmt > 1.0e-4) addTail (m[i], cab, len);
                    // microfono in campo lontano + differenza della posizione rispetto al centro
                    for (int f = 0; f < m[i].mic->nf; ++f) { BqD q; q.filt (sr, m[i].mic->ff[f]); q.run (xm.data(), 0, len); }
                    if (m[i].useDelta)
                        for (int f = 0; f < m[i].pos->nf; ++f) { BqD q; q.filt (sr, m[i].pos->f[f]); q.run (xm.data(), 0, len); }
                    const double g = m[i].weight * m[i].polarity;
                    for (int s = 0; s < len; ++s) acc[(size_t) s] += g * xm[(size_t) s];
                    const double wf = farWeight (m[i]);
                    wfar += std::abs (m[i].weight) * wf; wsum += std::abs (m[i].weight);
                }
                wfar = wsum > 0 ? wfar / wsum : 0;

                // mobile e altoparlante (comuni a tutti i percorsi)
                cabinetChain (cab, spk, wfar, len);
                speakerChain (spk, wfar, len);

                // coda: dissolvenza dell'ultimo 20%, poi normalizzazione (potenza rosa 80 Hz - 5 kHz = 1)
                const int tail = len / 5;
                for (int i = len - tail; i < len; ++i)
                    acc[(size_t) i] *= 0.5 + 0.5 * std::cos (kPiD * (double) (i - (len - tail)) / (double) tail);
                const double gn = normGain (acc.data(), len);
                for (int i = 0; i < len; ++i) out[i] = (float) (acc[(size_t) i] * gn);
                return len;
            }

            /** IR dell'utente gia' alla frequenza giusta: dissolvenza finale e stessa normalizzazione delle generate. */
            int finishUser (const std::vector<float>& ir, float* out)
            {
                const int len = std::min (cap, (int) ir.size());
                if (len < 1) return 0;
                for (int i = 0; i < len; ++i) acc[(size_t) i] = ir[(size_t) i];
                if ((int) ir.size() > len)
                {
                    const int tail = std::max (1, len / 10);
                    for (int i = len - tail; i < len; ++i)
                        acc[(size_t) i] *= 0.5 + 0.5 * std::cos (kPiD * (double) (i - (len - tail)) / (double) tail);
                }
                const double gn = normGain (acc.data(), len);
                for (int i = 0; i < len; ++i) out[i] = (float) (acc[(size_t) i] * gn);
                return len;
            }

        private:
            //==========================================================================
            /** Coni rispetto al centro del fronte (x) e al pavimento (y), in metri. */
            void buildGeometry (const i6::Cab& cab)
            {
                W = cab.w * 0.001; H = cab.h * 0.001; D = cab.d * 0.001;
                n = std::clamp (cab.nspk, 1, 8);
                for (int k = 0; k < n; ++k) { cx[k] = (cab.sx[k] * 0.01 - 0.5) * W; cy[k] = (1.0 - cab.sy[k] * 0.01) * H; }
                main = 0;
                for (int k = 1; k < n; ++k)
                    if (cy[k] > cy[main] + 1.0e-6 || (std::abs (cy[k] - cy[main]) < 1.0e-6 && cx[k] < cx[main])) main = k;
                // vicino per "fra due coni": stessa fila se c'e', altrimenti il piu' vicino
                nb = -1;
                double best = 1.0e9;
                for (int k = 0; k < n; ++k)
                {
                    if (k == main) continue;
                    const double dd = std::hypot (cx[k] - cx[main], cy[k] - cy[main]) + (std::abs (cy[k] - cy[main]) > 0.02 ? 10.0 : 0.0);
                    if (dd < best) { best = dd; nb = k; }
                }
            }

            static double farWeight (const MicPlace& m) noexcept { return m.kind == i6::Room ? 1.0 : m.d / (m.d + 0.3); }

            /** Microfoni dalla chiave: 1 o 2. */
            int placeMics (const IrKey& k, MicPlace* m) const
            {
                const double room = (double) k.room / kRoomSteps;
                auto place = [&] (MicPlace& p, int mic, int pos, double trim)
                {
                    p.mic = &i6::kMics[mic];
                    p.pos = &i6::kPositions[pos];
                    p.kind = p.pos->kind;
                    p.d = 0.01 * p.pos->distCm * trim;
                    p.useDelta = p.kind == i6::OnCone || p.kind == i6::Back || p.kind == i6::Between;
                    p.roomAmt = p.kind == i6::Room ? 0.6 + 0.4 * room : room;
                };
                const double trim = std::pow (4.0, 2.0 * ((double) k.dist / kDistSteps - 0.5));     // 1/4 ... 4 volte
                place (m[0], k.mic1, k.pos1, trim);
                m[0].seed = 0x9e3779b9u ^ (uint32_t) (k.cab * 131 + k.spk * 17 + k.mic1);
                if (k.mic2 < 0) return 1;
                const double b = (double) k.blend / kBlendSteps, e = (double) k.m2dist / kM2Steps;
                m[0].weight = 1.0 - b;
                if (k.m2mode == 0)
                {
                    // accanto al primo, come due microfoni legati insieme: stessa posizione, 2 cm piu' in alto, 0-40 cm piu' lontano
                    m[1] = m[0];
                    m[1].mic = &i6::kMics[k.mic2];
                    m[1].d = m[0].d + 0.40 * e;
                    m[1].lift = 0.02;
                }
                else
                {
                    const int pid = k.m2mode == 1 ? i6::kPosBack : i6::kPosRoom;
                    place (m[1], k.mic2, pid, 1.0);
                    m[1].d = k.m2mode == 1 ? 0.10 * (1.0 + 3.0 * e) : 1.5 * (1.0 + 2.0 * e);
                }
                m[1].weight = b;
                m[1].seed = 0x85ebca6bu ^ (uint32_t) (k.cab * 131 + k.spk * 17 + k.mic2 * 7 + k.m2mode);
                return 2;
            }

            double polar (const i6::Mic& mic, double cosPhi) const noexcept { return (1.0 - mic.alpha) + mic.alpha * cosPhi; }

            /** Direttivita' di un pistone: -3 dB a k a sin(theta) = 1,6. */
            double dirFreq (double sinT) const noexcept
            {
                if (sinT < 0.05) return 0;
                return std::clamp (1.6 * kSound / (2.0 * kPiD * a * sinT), 400.0, 18000.0);
            }

            int buildPaths (const MicPlace& m, const i6::Cab& cab, Path* p) const
            {
                int np = 0;
                auto add = [&] (const Path& q) { if (np < kMaxPaths && std::abs (q.g) > 1.0e-7) p[np++] = q; };
                const double open = cab.open;
                const double a2 = a * a * 0.5;
                const auto& mic = *m.mic;
                if (m.kind == i6::Back)
                {
                    // dietro il mobile: onda posteriore di ogni cono (in opposizione) e onda frontale che gira attorno al fianco
                    const double mx = cx[main], my = cy[main] + m.lift;
                    if (open > 0)
                    {
                        const double zb = std::max (0.05, D - 0.10) + m.d;      // dal retro del cono al microfono
                        double amax = 0;
                        for (int k = 0; k < n; ++k)
                        {
                            const double rho = std::hypot (mx - cx[k], my - cy[k]), R = std::sqrt (rho * rho + zb * zb);
                            amax = std::max (amax, a2 / std::sqrt (R * R + 0.25 * a * a));
                        }
                        for (int k = 0; k < n; ++k)
                        {
                            const double rho = std::hypot (mx - cx[k], my - cy[k]), R = std::sqrt (rho * rho + zb * zb);
                            Path q;
                            q.t = R / kSound;
                            const double A = a2 / std::sqrt (R * R + 0.25 * a * a);
                            q.g = -open * A * polar (mic, zb / R) * (A >= 0.7 * amax ? 1.0 : 0.6);
                            q.dirF = A >= 0.7 * amax ? 0.0 : dirFreq (rho / R);
                            q.proxR = std::sqrt (rho * rho + zb * zb + a * a);
                            add (q);
                        }
                        const double e = edgeDist (cx[main], cy[main]), L = e + D + std::sqrt (e * e + m.d * m.d);
                        Path q;
                        q.t = L / kSound; q.g = open * a2 / L * polar (mic, 0.3); q.lpF = kSound / (2.0 * (D + e)); q.proxR = L;
                        add (q);
                    }
                    else
                    {
                        // mobile chiuso (solo laboratorio): pannello posteriore che irradia la pressione interna, cupo
                        Path q;
                        const double R = m.d + 0.05;
                        q.t = R / kSound; q.g = -0.2 * a2 / std::sqrt (R * R + 0.25 * a * a); q.dirF = 300.0; q.lpF = 180.0; q.proxR = R + 0.3;
                        add (q);
                    }
                    return np;
                }

                // microfono davanti al baffle
                double mx, my;
                const double mz = std::max (0.003, m.d);
                if (m.kind == i6::Room) { mx = 0.0; my = 0.6 * H + 0.3; }
                else if (m.kind == i6::Between && nb >= 0) { mx = 0.5 * (cx[main] + cx[nb]); my = 0.5 * (cy[main] + cy[nb]); }
                else
                {
                    const double dir = cx[main] <= 1.0e-6 ? 1.0 : -1.0;          // verso il centro del mobile
                    mx = cx[main] + dir * m.pos->radial * a; my = cy[main];
                }
                my += m.lift;
                const double wf = farWeight (m);
                double amax = 0;
                for (int k = 0; k < n; ++k)
                {
                    const double rho = std::hypot (mx - cx[k], my - cy[k]);
                    amax = std::max (amax, a2 / std::sqrt (rho * rho + mz * mz + 0.25 * a * a));
                }
                for (int k = 0; k < n; ++k)
                {
                    const double rho = std::hypot (mx - cx[k], my - cy[k]), R = std::sqrt (rho * rho + mz * mz);
                    const double A = a2 / std::sqrt (rho * rho + mz * mz + 0.25 * a * a);
                    const bool mainSet = A >= 0.7 * amax;
                    Path q;
                    q.t = R / kSound;
                    q.g = A * polar (mic, mz / R) * (mainSet ? 1.0 : 0.5 + 0.5 * wf);       // ombra del cono vicino (stima)
                    q.dirF = mainSet ? 0.0 : dirFreq (rho / R);
                    q.proxR = std::sqrt (rho * rho + mz * mz + a * a);
                    add (q);
                }
                // onda posteriore dei mobili aperti: esce dal retro, gira attorno al fianco piu' vicino, in opposizione
                const double gR = std::min (1.0, 3.0 * m.roomAmt);
                if (open > 0)
                    for (int k = 0; k < n; ++k)
                    {
                        double em = 0;
                        const double e = edgeDist (cx[k], cy[k], &em, mx, my);
                        const double L = std::max (0.05, D - 0.10) + std::sqrt (D * D + e * e) + std::sqrt (em * em + mz * mz);
                        Path q;
                        q.t = L / kSound;
                        q.g = -open * a2 / L * polar (mic, mz / std::sqrt (em * em + mz * mz));
                        q.lpF = kSound / (2.0 * (D + e));
                        q.proxR = L;
                        add (q);
                        // riflessione sulla parete dietro al mobile (30 cm): parte della stanza
                        if (gR > 0)
                        {
                            Path w = q;
                            w.t += 0.6 / kSound; w.g *= 0.7 * gR * L / (L + 0.6); w.lpF *= 0.7; w.proxR = L + 0.6;
                            add (w);
                        }
                    }
                // diffrazione dai bordi del baffle (sinistro, destro, alto): sorgenti secondarie in opposizione
                {
                    const double ex[3] = { cx[main] + 0.5 * W, 0.5 * W - cx[main], H - cy[main] };
                    const double emx[3] = { mx + 0.5 * W, 0.5 * W - mx, H - my };
                    for (int j = 0; j < 3; ++j)
                    {
                        const double e = std::max (0.02, ex[j]), em = std::max (0.0, emx[j]);
                        const double Rm = std::sqrt (em * em + mz * mz), L = e + Rm;
                        Path q;
                        q.t = L / kSound;
                        q.g = -(1.0 / 6.0) * (double) n * a2 / L * polar (mic, mz / std::max (1.0e-6, Rm)) * (1.0 - std::exp (-mz / 0.05));
                        q.lpF = 6000.0;
                        q.proxR = L;
                        add (q);
                    }
                }
                // pavimento (per ogni cono), soffitto, pareti laterali, parete di fronte: sorgenti immagine
                if (gR > 0 || m.roomAmt > 0)
                {
                    const double rf = 0.7;
                    for (int k = 0; k < n; ++k)
                        addImage (np, p, mic, mx - cx[k], my + cy[k], mz, rf * gR, a2);
                    const double ccy = 0.5 * H;
                    const double ra = m.roomAmt * (double) n * 0.6;
                    addImage (np, p, mic, mx, (2.0 * kRoomH - ccy) - my, mz, ra, a2);                    // soffitto
                    addImage (np, p, mic, (2.0 * kRoomW2) - mx, my - ccy, mz, ra, a2);                   // parete destra
                    addImage (np, p, mic, (2.0 * kRoomW2) + mx, my - ccy, mz, ra, a2);                   // parete sinistra
                    addImage (np, p, mic, mx, my - ccy, mz - 2.0 * kRoomFront, ra, a2);                  // parete di fronte
                }
                return np;
            }

            /** Sorgente immagine con vettore (mic - immagine) = (vx, vy, vz). */
            void addImage (int& np, Path* p, const i6::Mic& mic, double vx, double vy, double vz, double g, double a2) const
            {
                if (g <= 0 || np >= kMaxPaths) return;
                const double R = std::sqrt (vx * vx + vy * vy + vz * vz);
                Path q;
                q.t = R / kSound;
                q.g = g * a2 / std::sqrt (R * R + 0.25 * a * a) * polar (mic, vz / R);
                q.dirF = dirFreq (std::sqrt (vx * vx + vy * vy) / R);
                if (q.dirF <= 0) q.dirF = 16000.0;
                q.lpF = 7000.0;
                q.proxR = R;
                p[np++] = q;
            }

            /** Distanza di (x, y) dal bordo piu' vicino (sinistro, destro, alto; il basso poggia sul pavimento);
                em = distanza del microfono (mx, my) dallo stesso bordo. */
            double edgeDist (double x, double y, double* em = nullptr, double mx = 0, double my = 0) const noexcept
            {
                const double e[3] = { x + 0.5 * W, 0.5 * W - x, H - y };
                const double m[3] = { mx + 0.5 * W, 0.5 * W - mx, H - my };
                int j = 0;
                for (int i = 1; i < 3; ++i) if (e[i] < e[j]) j = i;
                if (em != nullptr) *em = std::max (0.0, m[j]);
                return std::max (0.02, e[j]);
            }

            /** Un percorso: impulso al ritardo (frazionario: sinc finestrato a 8 prese) con i suoi filtri, in xm. */
            void renderPath (const Path& q, double tmin, const i6::Mic& mic, int len)
            {
                const double pos = (q.t - tmin) * sr;
                const int i0 = (int) std::floor (pos);
                const double fr = pos - i0;
                if (i0 >= len - 1) return;
                const int from = std::max (0, i0 - 4);
                std::fill (tmp.begin() + from, tmp.begin() + len, 0.0);
                if (fr < 1.0e-6) tmp[(size_t) i0] = q.g;
                else if (i0 < 4) { tmp[(size_t) i0] += q.g * (1.0 - fr); tmp[(size_t) i0 + 1] += q.g * fr; }
                else
                    for (int j = -3; j <= 4; ++j)
                    {
                        const int idx = i0 + j;
                        if (idx < 0 || idx >= len) continue;
                        const double x = (double) j - fr;
                        const double s = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (kPiD * x) / (kPiD * x);
                        const double wnd = 0.5 + 0.5 * std::cos (kPiD * x / 4.5);
                        tmp[(size_t) idx] += q.g * s * wnd;
                    }
                BqD f;
                if (q.dirF > 0) { f.lp (sr, q.dirF, 0.6); f.run (tmp.data(), from, len); }
                if (q.lpF > 0) { f.lp1 (sr, q.lpF); f.run (tmp.data(), from, len); }
                if (q.proxR > 0 && mic.alpha > 0)
                {
                    f.prox (sr, mic.alpha * kSound / q.proxR, 10.0);
                    f.run (tmp.data(), from, len);
                }
                for (int i = from; i < len; ++i) xm[(size_t) i] += tmp[(size_t) i];
            }

            /** Coda diffusa della stanza (rumore deterministico che decade, T60 0,35 s, assorbimento delle alte). */
            void addTail (const MicPlace& m, const i6::Cab&, int len)
            {
                const double t60 = 0.35, t0 = 0.009 + 0.5 * m.d / kSound;
                const double a2 = a * a * 0.5;
                // energia diffusa ~ indipendente dalla distanza: n coni incoerenti, distanza critica 1,2 m
                const double polarE = (1.0 - m.mic->alpha) * (1.0 - m.mic->alpha) + m.mic->alpha * m.mic->alpha / 3.0;
                const double E = m.roomAmt * m.roomAmt * (double) n * (a2 / 1.2) * (a2 / 1.2) * polarE;
                const double g = std::sqrt (E * 13.8 / (sr * t60)) * std::sqrt (3.0);
                uint32_t s = m.seed | 1u;
                const int i0 = std::max (1, (int) (t0 * sr));
                std::fill (tmp.begin(), tmp.begin() + len, 0.0);
                for (int i = i0; i < len; ++i)
                {
                    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                    const double u = (double) (s & 0xffffff) / 8388608.0 - 1.0;
                    const double t = (double) (i - i0) / sr;
                    const double rise = std::min (1.0, (double) (i - i0) / (0.004 * sr));
                    tmp[(size_t) i] = g * u * rise * std::exp (-6.9 * t / t60);
                }
                BqD f;
                f.lp1 (sr, 4500.0); f.run (tmp.data(), i0, len);
                f.hp (sr, 90.0, 0.7); f.run (tmp.data(), i0, len);
                for (int i = i0; i < len; ++i) xm[(size_t) i] += tmp[(size_t) i];
            }

            /** Passa-alto e risonanza del sistema, porta reflex, modi dell'aria interna, risonanza del pannello. */
            void cabinetChain (const i6::Cab& cab, const i6::Speaker& spk, double wf, int len)
            {
                double sf = 1.0;
                if (spk.fs > 0 && cab.defFs > 0) sf = std::clamp ((double) spk.fs / cab.defFs, 0.75, 1.35);
                BqD f;
                f.hp (sr, cab.hp * sf, 0.707); f.run (acc.data(), 0, len);
                f.peak (sr, cab.fres * sf, cab.qres, cab.gres); f.run (acc.data(), 0, len);
                if (cab.ported && cab.fb > 0)
                {
                    // vicino al cono: buco all'accordo (il cono quasi fermo, irradia la porta); lontano: la porta riempie
                    f.peak (sr, cab.fb, 2.0, -8.0 * (1.0 - wf) + 2.0 * wf); f.run (acc.data(), 0, len);
                    if (wf > 0.3) { f.hp (sr, 0.8 * cab.fb, 0.7); f.run (acc.data(), 0, len); }
                }
                const double t = std::max (0.009, cab.thick * 0.001);
                const double Li[3] = { W - 2 * t, H - 2 * t, D - 2 * t };
                const auto& mat = i6::kMaterials[std::clamp (cab.material, 0, i6::kNumMaterials - 1)];
                const double closed = 1.0 - cab.open;
                for (int i = 0; i < 3; ++i)
                {
                    if (i == 2 && cab.open >= 1.0) continue;               // senza retro non c'e' il modo in profondita'
                    const double fm = kSound / (2.0 * std::max (0.1, Li[i]));
                    const double gm = mat.modeDb * (0.35 + 0.65 * closed) * (i == 2 ? 0.7 : 1.0);
                    f.peak (sr, fm, mat.modeQ, gm); f.run (acc.data(), 0, len);
                }
                // pannello piu' grande (retro dei chiusi, cielo degli aperti): f = pi/2 sqrt(D/rho h) (1/Lx^2 + 1/Ly^2)
                const double Lx = W, Ly = cab.open >= 1.0 ? D : H;
                const double Dp = mat.E * t * t * t / (12.0 * (1.0 - 0.09)), rh = mat.rho * t;
                const double fp = std::clamp (0.5 * kPiD * std::sqrt (Dp / rh) * (1.0 / (Lx * Lx) + 1.0 / (Ly * Ly)), 60.0, 400.0);
                f.peak (sr, fp, mat.panelQ, mat.panelDb); f.run (acc.data(), 0, len);
            }

            void speakerChain (const i6::Speaker& spk, double wf, int len)
            {
                BqD f;
                for (int b = 0; b < 5; ++b)
                {
                    const double fb = spk.bell[b][0], q = spk.bell[b][1];
                    double g = spk.bell[b][2];
                    if (fb <= 0) continue;
                    if (g > 0 && fb > 1200.0) g *= kPresence;
                    f.peak (sr, fb, q, g); f.run (acc.data(), 0, len);
                }
                if (std::abs (kVoiceShelfDb) > 1.0e-9) { f.shelf (sr, kVoiceShelfHz, kVoiceShelfDb, true); f.run (acc.data(), 0, len); }
                const double lp = spk.lp * (1.0 - 0.08 * wf);            // lontano il microfono vede piu' cono fuori asse
                f.lp (sr, lp, 0.8); f.run (acc.data(), 0, len);
                f.lp (sr, lp * 1.18, 0.6); f.run (acc.data(), 0, len);
            }

            /** Guadagno che porta a 1 la potenza media pesata in rosa (1/f) fra 80 Hz e 5 kHz. */
            double normGain (const double* y, int len)
            {
                const int N = fft->getSize();
                std::fill (spec.begin(), spec.end(), 0.0f);
                for (int i = 0; i < std::min (len, N); ++i) spec[(size_t) i] = (float) y[i];
                fft->performFrequencyOnlyForwardTransform (spec.data(), true);
                double pw = 0, wsum = 0;
                const int k0 = std::max (1, (int) std::ceil (80.0 * N / sr)), k1 = std::min (N / 2, (int) (5000.0 * N / sr));
                for (int k = k0; k <= k1; ++k)
                {
                    const double wgt = 1.0 / (double) k, mag = spec[(size_t) k];
                    pw += wgt * mag * mag; wsum += wgt;
                }
                return 1.0 / std::sqrt (std::max (1.0e-20, pw / std::max (1.0e-20, wsum)));
            }

            static constexpr int kMaxPaths = 64;
            static constexpr double kRoomH = 2.6, kRoomW2 = 2.2, kRoomFront = 3.5;   // stanza: soffitto, mezza larghezza, parete di fronte
            double sr = 48000, a = 0.127, W = 0.7, H = 0.7, D = 0.3;
            double cx[8] {}, cy[8] {};
            int n = 1, main = 0, nb = -1, cap = 0;
            Path paths[2][kMaxPaths];
            std::vector<double> acc, xm, tmp;
            std::unique_ptr<juce::dsp::FFT> fft;
            std::vector<float> spec;
        };

        //==============================================================================
        /** Convoluzione partizionata uniforme a latenza zero, con IR sostituibili senza click.
            Testa (prime B prese) nel dominio del tempo; partizioni successive con overlap-save su FFT da 2B e linea
            di ritardo spettrale dell'ingresso condivisa da tutte le IR. Gli slot si riempiono fuori dal thread
            audio (load) e si pubblicano con uno scambio atomico; il thread audio li prende al bordo di un blocco
            e dissolve dalla IR vecchia alla nuova. Stato degli slot: 0 libero, 1 occupato. */
        class PartConv
        {
        public:
            static constexpr int numSlots = 4;

            void prepare (int partition, int maxTaps, double sr)
            {
                B = partition; N = 2 * B; nb = B + 1;
                int order = 1;
                while ((1 << order) < N) ++order;
                fftA = std::make_unique<juce::dsp::FFT> (order);
                fftW = std::make_unique<juce::dsp::FFT> (order);
                maxParts = std::max (1, (maxTaps + B - 1) / B);
                fdl.assign ((size_t) (maxParts * 2 * nb), 0.0f);
                inBuf.assign ((size_t) (2 * B), 0.0f);
                work.assign ((size_t) (2 * N), 0.0f);
                workW.assign ((size_t) (2 * N), 0.0f);
                accR.assign ((size_t) nb, 0.0f); accI.assign ((size_t) nb, 0.0f);
                tailCur.assign ((size_t) B, 0.0f); tailOld.assign ((size_t) B, 0.0f);
                for (auto& s : slots)
                {
                    s.spec.assign ((size_t) (maxParts * 2 * nb), 0.0f);
                    s.head.assign ((size_t) B, 0.0f);
                    s.parts = 0;
                    s.state.store (0);
                }
                fadeLen = std::max (1, (int) (0.03 * sr));
                ready.store (-1);
                cur = old = -1;
                reset();
            }

            void reset() noexcept
            {
                std::fill (fdl.begin(), fdl.end(), 0.0f);
                std::fill (inBuf.begin(), inBuf.end(), 0.0f);
                std::fill (tailCur.begin(), tailCur.end(), 0.0f);
                std::fill (tailOld.begin(), tailOld.end(), 0.0f);
                fdlPos = 0; pos = 0;
                if (old >= 0) { slots[old].state.store (0, std::memory_order_release); old = -1; }
                fadePos = fadeLen;
            }

            /** Thread non audio: uno slot libero (0..3) o -1. */
            int acquire() noexcept
            {
                for (int i = 0; i < numSlots; ++i)
                {
                    int expect = 0;
                    if (slots[i].state.compare_exchange_strong (expect, 1, std::memory_order_acq_rel)) return i;
                }
                return -1;
            }

            /** Thread non audio: spettri delle partizioni della IR nello slot acquisito. */
            void fill (int si, const float* ir, int len) noexcept
            {
                auto& s = slots[si];
                len = std::clamp (len, 1, maxParts * B);
                s.parts = (len + B - 1) / B;
                for (int t = 0; t < B; ++t) s.head[(size_t) (B - 1 - t)] = t < len ? ir[t] : 0.0f;
                for (int p = 1; p < s.parts; ++p)
                {
                    std::fill (workW.begin(), workW.end(), 0.0f);
                    for (int t = 0; t < B; ++t) { const int i = p * B + t; workW[(size_t) t] = i < len ? ir[i] : 0.0f; }
                    fftW->performRealOnlyForwardTransform (workW.data(), true);
                    float* re = s.spec.data() + (size_t) (p * 2 * nb);
                    float* im = re + nb;
                    for (int k = 0; k < nb; ++k) { re[k] = workW[(size_t) (2 * k)]; im[k] = workW[(size_t) (2 * k + 1)]; }
                }
            }

            /** Thread non audio: pubblica lo slot; uno pubblicato e mai preso torna libero. */
            void publish (int si) noexcept
            {
                const int prev = ready.exchange (si, std::memory_order_acq_rel);
                if (prev >= 0) slots[prev].state.store (0, std::memory_order_release);
            }

            /** In prepare(): lo slot diventa subito quello corrente, senza dissolvenza. */
            void installNow (int si) noexcept
            {
                if (cur >= 0 && cur != si) slots[cur].state.store (0, std::memory_order_release);
                cur = si; old = -1; fadePos = fadeLen;
                computeTail (cur, tailCur.data());
            }

            /** Mono in place, n qualsiasi. */
            void process (float* x, int n) noexcept
            {
                if (cur < 0) return;
                int i = 0;
                while (i < n)
                {
                    const int m = std::min (n - i, B - pos);
                    const float* hc = slots[cur].head.data();
                    const float* ho = old >= 0 ? slots[old].head.data() : nullptr;
                    for (int j = 0; j < m; ++j)
                    {
                        inBuf[(size_t) (B + pos)] = x[i + j];
                        const float* xs = inBuf.data() + pos + 1;
                        float y = tailCur[(size_t) pos];
                        for (int u = 0; u < B; ++u) y += hc[u] * xs[u];
                        if (ho != nullptr)
                        {
                            float yo = tailOld[(size_t) pos];
                            for (int u = 0; u < B; ++u) yo += ho[u] * xs[u];
                            const float t = std::min (1.0f, (float) fadePos / (float) fadeLen);
                            const float gn = 0.5f - 0.5f * std::cos (3.14159265f * t);
                            y = gn * y + (1.0f - gn) * yo;
                            ++fadePos;
                        }
                        x[i + j] = y;
                        ++pos;
                    }
                    i += m;
                    if (pos == B) endBlock();
                }
            }

            int partitions (int si) const noexcept { return si >= 0 ? slots[si].parts : 0; }
            int current() const noexcept { return cur; }
            bool fading() const noexcept { return old >= 0; }

        private:
            void endBlock() noexcept
            {
                pos = 0;
                // spettro del fotogramma [blocco precedente | blocco corrente] nella linea di ritardo
                std::copy (inBuf.begin(), inBuf.end(), work.begin());
                std::fill (work.begin() + N, work.end(), 0.0f);
                fftA->performRealOnlyForwardTransform (work.data(), true);
                float* re = fdl.data() + (size_t) (fdlPos * 2 * nb);
                float* im = re + nb;
                for (int k = 0; k < nb; ++k) { re[k] = work[(size_t) (2 * k)]; im[k] = work[(size_t) (2 * k + 1)]; }
                std::copy (inBuf.begin() + B, inBuf.end(), inBuf.begin());
                // fine della dissolvenza: lo slot vecchio torna libero
                if (old >= 0 && fadePos >= fadeLen) { slots[old].state.store (0, std::memory_order_release); old = -1; }
                // nuova IR pronta: parte la dissolvenza (una alla volta)
                if (old < 0)
                {
                    const int r = ready.exchange (-1, std::memory_order_acq_rel);
                    if (r >= 0) { old = cur; cur = r; fadePos = 0; }
                }
                computeTail (cur, tailCur.data());
                if (old >= 0) computeTail (old, tailOld.data());
                fdlPos = (fdlPos + 1) % maxParts;
            }

            /** Coda del prossimo blocco: somma delle partizioni p >= 1 sugli spettri passati, una FFT inversa. */
            void computeTail (int si, float* out) noexcept
            {
                const auto& s = slots[si];
                if (s.parts <= 1) { std::fill (out, out + B, 0.0f); return; }
                std::fill (accR.begin(), accR.end(), 0.0f);
                std::fill (accI.begin(), accI.end(), 0.0f);
                float* ar = accR.data();
                float* ai = accI.data();
                for (int p = 1; p < s.parts; ++p)
                {
                    const int idx = ((fdlPos - (p - 1)) % maxParts + maxParts) % maxParts;
                    const float* xr = fdl.data() + (size_t) (idx * 2 * nb);
                    const float* xi = xr + nb;
                    const float* hr = s.spec.data() + (size_t) (p * 2 * nb);
                    const float* hi = hr + nb;
                    for (int k = 0; k < nb; ++k)
                    {
                        ar[k] += xr[k] * hr[k] - xi[k] * hi[k];
                        ai[k] += xr[k] * hi[k] + xi[k] * hr[k];
                    }
                }
                for (int k = 0; k < nb; ++k) { work[(size_t) (2 * k)] = ar[k]; work[(size_t) (2 * k + 1)] = ai[k]; }
                std::fill (work.begin() + 2 * nb, work.end(), 0.0f);
                fftA->performRealOnlyInverseTransform (work.data());
                std::copy (work.begin() + B, work.begin() + N, out);
            }

            struct Slot
            {
                std::vector<float> spec, head;
                int parts = 0;
                std::atomic<int> state { 0 };
            };
            int B = 64, N = 128, nb = 65, maxParts = 1, fdlPos = 0, pos = 0, cur = -1, old = -1, fadeLen = 1440, fadePos = 0;
            std::unique_ptr<juce::dsp::FFT> fftA, fftW;
            std::vector<float> fdl, inBuf, work, workW, accR, accI, tailCur, tailOld;
            Slot slots[numSlots];
            std::atomic<int> ready { -1 };
        };

        //==============================================================================
        /** Lettura di un IR dell'utente (stessi controlli dell'IR loader a catalogo), mono. */
        bool readUserIR (const juce::File& f, std::vector<float>& out, double& rate)
        {
            const auto ext = f.getFileExtension().toLowerCase();
            if (ext != ".wav" && ext != ".aif" && ext != ".aiff") return false;
            if (! pt::namsafe::isSafeLocalFile (f.getFullPathName(), 32 * 1024 * 1024)) return false;
            if (! f.existsAsFile() || f.getSize() < 44 || f.getSize() > 32 * 1024 * 1024) return false;
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
            if (reader == nullptr || ! std::isfinite (reader->sampleRate) || reader->sampleRate < 8000.0 || reader->sampleRate > 384000.0
                || reader->numChannels < 1 || reader->numChannels > 2 || reader->lengthInSamples < 1
                || reader->lengthInSamples > (juce::int64) (reader->sampleRate * 10.0))
                return false;
            const int samples = (int) reader->lengthInSamples;
            juce::AudioBuffer<float> tmp ((int) reader->numChannels, samples);
            if (! reader->read (&tmp, 0, samples, 0, true, reader->numChannels > 1)) return false;
            out.assign ((size_t) samples, 0.0f);
            float peak = 0;
            for (int i = 0; i < samples; ++i)
            {
                float v = tmp.getSample (0, i);
                if (tmp.getNumChannels() > 1) v = 0.5f * (v + tmp.getSample (1, i));
                if (! std::isfinite (v)) return false;
                peak = std::max (peak, std::abs (v));
                out[(size_t) i] = v;
            }
            if (peak > 64.0f || peak <= 0.0f) return false;
            rate = reader->sampleRate;
            return true;
        }

        /** Ricampionamento (interpolazione di Lagrange di JUCE) alla frequenza della sessione, fino a maxLen campioni. */
        std::vector<float> resampleIR (const std::vector<float>& in, double from, double to, int maxLen)
        {
            if (std::abs (from - to) < 0.5)
                return std::vector<float> (in.begin(), in.begin() + std::min ((int) in.size(), maxLen));
            const double ratio = from / to;
            const int outLen = std::min (maxLen, (int) std::ceil ((double) in.size() / ratio));
            std::vector<float> src (in);
            src.resize (src.size() + 8, 0.0f);
            std::vector<float> out ((size_t) std::max (1, outLen), 0.0f);
            juce::LagrangeInterpolator li;
            li.process (ratio, src.data(), out.data(), outLen);
            return out;
        }

        //==============================================================================
        /** Lista di indici da "id,id,..." (sconosciuti saltati); fallback se vuota. */
        template <typename F>
        std::vector<int> idList (const std::string& s, F&& find, int fallback)
        {
            std::vector<int> out;
            size_t i = 0;
            while (i <= s.size())
            {
                auto j = s.find (',', i);
                if (j == std::string::npos) j = s.size();
                const auto tok = s.substr (i, j - i);
                if (! tok.empty()) { const int k = find (tok.c_str()); if (k >= 0) out.push_back (k); }
                i = j + 1;
            }
            if (out.empty() && fallback >= 0) out.push_back (fallback);
            return out;
        }

        //==============================================================================
        class I6Cab final : public Effect
        {
        public:
            explicit I6Cab (const ModelDef& d) : Effect (d)
            {
                const Config cfg (d.config);
                const auto cabId = cfg.str ("cab", "cbm1960b");
                lab = cabId == "lab";
                cabIndex = lab ? i6::kLabFormats[8] : std::max (0, i6::findCab (cabId.c_str()));
                // altoparlanti: banchi separati da '|' (laboratorio) o una sola lista
                const auto spkStr = cfg.str ("spk", "");
                size_t i = 0;
                while (i <= spkStr.size())
                {
                    auto j = spkStr.find ('|', i);
                    if (j == std::string::npos) j = spkStr.size();
                    banks.push_back (idList (spkStr.substr (i, j - i), i6::findSpeaker, i6::kCabs[cabIndex].defSpk));
                    i = j + 1;
                    if (j == spkStr.size()) break;
                }
                if (banks.empty()) banks.push_back ({ i6::kCabs[cabIndex].defSpk });
                positions = idList (cfg.str ("pos", ""), i6::findPosition, i6::kPosCapEdge);
                m2alt = cfg.str ("m2", "room") == "back" ? 1 : 2;
                pSpk = role (*this, "speaker", 0.0f); pMic = role (*this, "mic", 0.0f); pPos = role (*this, "position", 0.0f);
                pDist = role (*this, "dist", 0.5f); pRoom = role (*this, "room", 0.15f); pMic2 = role (*this, "mic2", 0.0f);
                pBlend = role (*this, "blend", 0.5f); pM2Dist = role (*this, "m2dist", 0.0f); pM2Pos = role (*this, "m2pos", 0.0f);
                pLo = role (*this, "locut", 0.0f); pHi = role (*this, "hicut", 0.0f); pLevel = role (*this, "level", 0.5f);
                pFormat = role (*this, "format", 8.0f); pBank = role (*this, "bank", 0.0f);
                if (! pPos.present()) defaultPos = std::max (0, (int) (std::find (positions.begin(), positions.end(), i6::kPosCapEdge) - positions.begin()));
            }

            ~I6Cab() override { stopWorker(); }

            void prepare (double s, int maxBlock) override
            {
                stopWorker();
                sr = (float) s; blk = std::max (16, maxBlock);
                const int B = s <= 50000.0 ? 64 : (s <= 100000.0 ? 128 : 256);
                const int maxTaps = (int) std::ceil (0.5 * s);
                gen.prepare (s, maxTaps);
                irBuf.assign ((size_t) maxTaps, 0.0f);
                conv.prepare (B, maxTaps, s);
                work.assign ((size_t) blk, 0.0f);
                {
                    const std::lock_guard<std::mutex> lk (userLock);
                    if (! loadedFile.empty() && userRaw.empty()) loadUserRaw (loadedFile);
                }
                built = currentKey();
                const int si = conv.acquire();
                conv.fill (si, irBuf.data(), buildIR (built));
                conv.installNow (si);
                loS.set (s, 20); hiS.set (s, 20); vS.set (s, 20);
                reset();
                startWorker();
            }

            void reset() override { conv.reset(); hp1.reset(); hp2.reset(); lp1.reset(); lp2.reset(); first = true; }

            bool acceptsFiles() const override { return true; }
            bool loadFile (const std::string& path) override
            {
                const std::lock_guard<std::mutex> lk (userLock);
                if (! loadUserRaw (path)) return false;
                loadedFile = juce::File (path).getFullPathName().toStdString();
                return true;
            }

            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const float loT = logMap (pLo.get (*this), 20.0f, 400.0f), hiT = logMap (pHi.get (*this), 20000.0f, 2000.0f);
                const float vT = volKnob (pLevel.get (*this), 0.5f, 2.5f);
                if (first) { loS.snap (loT); hiS.snap (hiT); vS.snap (vT); first = false; }
                for (int off = 0; off < n; off += blk)
                {
                    const int m = std::min (blk, n - off);
                    for (int i = 0; i < m; ++i) work[(size_t) i] = monoIn (ch, numCh, off + i);
                    conv.process (work.data(), m);
                    for (int i = 0; i < m; ++i)
                    {
                        if ((i & 7) == 0)
                        {
                            const float lo = loS.next (loT), hi = hiS.next (hiT);
                            hp1.set (sr, lo, 0.5412); hp2.set (sr, lo, 1.3066); lp1.set (sr, hi, 0.5412); lp2.set (sr, hi, 1.3066);
                        }
                        const float y = vS.next (vT) * lp2.lp (lp1.lp (hp2.hp (hp1.hp (work[(size_t) i]))));
                        for (int c = 0; c < numCh; ++c) ch[c][off + i] = y;
                    }
                }
            }

            /** Per i banchi di prova: aspetta (al massimo ms) che il thread di lavoro abbia pubblicato la IR dei comandi
                attuali e che il thread audio l'abbia presa. */
            bool settled() const noexcept { return builtAtomic.load() == keyHash (currentKey()); }

        private:
            //==========================================================================
            IrKey currentKey() const
            {
                IrKey k;
                k.user = userVersion.load();
                if (k.user > 0) return k;
                const int fmt = lab ? std::clamp (pFormat.step (*this), 0, 11) : -1;
                k.cab = lab ? i6::kLabFormats[fmt] : cabIndex;
                const auto& bank = banks[(size_t) std::clamp (lab ? pBank.step (*this) : 0, 0, (int) banks.size() - 1)];
                k.spk = bank[(size_t) std::clamp (pSpk.step (*this), 0, (int) bank.size() - 1)];
                k.mic1 = std::clamp (pMic.present() ? pMic.step (*this) : 0, 0, i6::kNumMics - 1);
                k.pos1 = positions[(size_t) std::clamp (pPos.present() ? pPos.step (*this) : defaultPos, 0, (int) positions.size() - 1)];
                k.dist = (int) std::lround (std::clamp (pDist.get (*this), 0.0f, 1.0f) * kDistSteps);
                k.room = (int) std::lround (std::clamp (pRoom.get (*this), 0.0f, 1.0f) * kRoomSteps);
                const int m2 = pMic2.present() ? pMic2.step (*this) : 0;
                k.mic2 = m2 <= 0 ? -1 : std::min (m2 - 1, i6::kNumMics - 1);
                if (k.mic2 >= 0)
                {
                    k.blend = (int) std::lround (std::clamp (pBlend.get (*this), 0.0f, 1.0f) * kBlendSteps);
                    k.m2dist = (int) std::lround (std::clamp (pM2Dist.get (*this), 0.0f, 1.0f) * kM2Steps);
                    k.m2mode = pM2Pos.step (*this) > 0 ? m2alt : 0;
                }
                else { k.blend = 0; k.m2dist = 0; k.m2mode = 0; }
                return k;
            }

            static uint64_t keyHash (const IrKey& k) noexcept
            {
                const int v[11] = { k.cab, k.spk, k.mic1, k.pos1, k.mic2, k.m2mode, k.user, k.dist, k.room, k.blend, k.m2dist };
                uint64_t h = 1469598103934665603ull;
                for (int x : v) { h ^= (uint64_t) (uint32_t) x; h *= 1099511628211ull; }
                return h;
            }

            /** IR della chiave in irBuf (thread di lavoro o prepare). */
            int buildIR (const IrKey& k)
            {
                if (k.user > 0)
                {
                    std::vector<float> raw;
                    double rate = sr;
                    {
                        const std::lock_guard<std::mutex> lk (userLock);
                        raw = userRaw; rate = userRate;
                    }
                    if (! raw.empty())
                    {
                        const auto ir = resampleIR (raw, rate, sr, gen.capacity());
                        const int len = gen.finishUser (ir, irBuf.data());
                        if (len > 0) return len;
                    }
                }
                IrKey g = k;
                if (g.user > 0) { g = IrKey(); g.cab = cabIndex; g.spk = banks[0][0]; g.pos1 = positions[0]; }
                return gen.generate (g, i6::kCabs[g.cab], irBuf.data());
            }

            bool loadUserRaw (const std::string& path)
            {
                std::vector<float> raw;
                double rate = 48000;
                if (! readUserIR (juce::File (path), raw, rate)) return false;
                userRaw = std::move (raw); userRate = rate;
                userVersion.store (userVersion.load() + 1);
                return true;
            }

            //==========================================================================
            void startWorker()
            {
                quit = false;
                builtAtomic.store (keyHash (built));
                worker = std::thread ([this] { run(); });
            }

            void stopWorker()
            {
                {
                    const std::lock_guard<std::mutex> lk (wakeLock);
                    quit = true;
                }
                wake.notify_all();
                if (worker.joinable()) worker.join();
            }

            void run()
            {
                std::unique_lock<std::mutex> lk (wakeLock);
                while (! quit)
                {
                    wake.wait_for (lk, std::chrono::milliseconds (4));
                    if (quit) break;
                    lk.unlock();
                    const IrKey k = currentKey();
                    if (k != built)
                    {
                        const int si = conv.acquire();
                        if (si >= 0)
                        {
                            conv.fill (si, irBuf.data(), buildIR (k));
                            conv.publish (si);
                            built = k;
                            builtAtomic.store (keyHash (k));
                        }
                    }
                    lk.lock();
                }
            }

            CabGen gen;
            PartConv conv;
            std::vector<float> irBuf, work;
            std::vector<std::vector<int>> banks;
            std::vector<int> positions;
            int cabIndex = 0, m2alt = 2, defaultPos = 1;
            bool lab = false;
            IrKey built;
            std::atomic<uint64_t> builtAtomic { 0 };

            std::thread worker;
            std::mutex wakeLock;
            std::condition_variable wake;
            bool quit = false;

            std::mutex userLock;
            std::vector<float> userRaw;
            double userRate = 48000;
            std::atomic<int> userVersion { 0 };

            float sr = 48000;
            int blk = 512;
            bool first = true;
            RoleParam pSpk, pMic, pPos, pDist, pRoom, pMic2, pBlend, pM2Dist, pM2Pos, pLo, pHi, pLevel, pFormat, pBank;
            Svf hp1, hp2, lp1, lp2;
            Smooth loS, hiS, vS;
        };
    }

    std::unique_ptr<Effect> makeI6Cab (const ModelDef& d, const std::string& type)
    {
        if (type == "i6cab") return std::make_unique<I6Cab> (d);
        return nullptr;
    }

    /** Banchi di prova (fuori dal prodotto, dichiarata nel banco): true quando il thread di lavoro ha calcolato la IR dei comandi attuali. */
    bool i6CabSettled (const Effect& fx)
    {
        if (auto* c = dynamic_cast<const I6Cab*> (&fx)) return c->settled();
        return true;
    }
}
