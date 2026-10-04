/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "RealMod.h"
#include "Theme.h"
#include "../engine/NamSecurity.h"
#include <cstring>
#include <tuple>

namespace pt::ui
{
    using namespace pt::engine;

    const ModelDef* visualDef (const ModelDef* d)
    {
        if (d == nullptr) return nullptr;
        juce::SharedResourcePointer<ThemeManager> themes;
        if (! themes->realMode()) return d;
        const auto* r = findRealModel (d->id);
        return r != nullptr ? r : d;
    }

    namespace
    {
        /** Sigla e nome della replica, con quelli personalizzati della lista REAL MOD in uso. */
        bool labelsOf (const ModelDef* d, juce::String& code, juce::String& name)
        {
            const auto* v = visualDef (d);
            if (v == nullptr) return false;
            code = v->code; name = v->name;
            if (v == d) return true;
            juce::SharedResourcePointer<RealPhotos> photos;
            juce::String c, n;
            photos->labelsFor (*v, c, n);
            if (c.isNotEmpty()) code = c;
            if (n.isNotEmpty()) name = n;
            return true;
        }
    }

    juce::String visualCode (const ModelDef* d)
    {
        juce::String c, n;
        labelsOf (d, c, n);
        return c;
    }

    juce::String visualName (const ModelDef* d)
    {
        juce::String c, n;
        labelsOf (d, c, n);
        return n;
    }

    namespace
    {
        constexpr juce::int64 maxPhotoBytes = 25 * 1024 * 1024, maxSidecarBytes = 64 * 1024;
        constexpr int maxSide = 8000, minSide = 64;

        /** Dimensioni dichiarate nell'intestazione, lette prima di decodificare (niente "bombe" di pixel). */
        bool headerSize (const juce::MemoryBlock& head, bool png, int& w, int& h)
        {
            const auto* b = static_cast<const uint8_t*> (head.getData());
            const size_t n = head.getSize();
            if (png)
            {
                static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
                if (n < 24 || std::memcmp (b, sig, 8) != 0 || std::memcmp (b + 12, "IHDR", 4) != 0) return false;
                auto be32 = [b] (size_t p) { return (uint32_t) b[p] << 24 | (uint32_t) b[p + 1] << 16 | (uint32_t) b[p + 2] << 8 | b[p + 3]; };
                const uint32_t pw = be32 (16), ph = be32 (20);
                if (pw > (uint32_t) maxSide || ph > (uint32_t) maxSide) { w = maxSide + 1; h = maxSide + 1; return true; }
                w = (int) pw; h = (int) ph;
                return true;
            }
            if (n < 4 || b[0] != 0xFF || b[1] != 0xD8 || b[2] != 0xFF) return false;
            size_t p = 2;
            while (p + 4 <= n)
            {
                if (b[p] != 0xFF) return false;
                while (p < n && b[p] == 0xFF) ++p;                     // byte di riempimento
                if (p >= n) return false;
                const uint8_t m = b[p++];
                if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue;
                if (m == 0xD9 || m == 0xDA) return false;                // fine o dati prima del SOF
                if (p + 2 > n) return false;
                const size_t len = (size_t) b[p] << 8 | b[p + 1];
                if (len < 2) return false;
                if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC)
                {
                    if (p + 7 > n) return false;
                    h = (int) ((size_t) b[p + 3] << 8 | b[p + 4]);
                    w = (int) ((size_t) b[p + 5] << 8 | b[p + 6]);
                    return true;
                }
                p += len;
            }
            return false;
        }

        /** Riquadro del pedale nella foto: pixel diversi dal colore del bordo (o non trasparenti). */
        juce::Rectangle<int> autoCrop (const juce::Image& img)
        {
            const int W = img.getWidth(), H = img.getHeight();
            const float k = juce::jmin (1.0f, 400.0f / (float) juce::jmax (W, H));
            const int w = juce::jmax (8, (int) (W * k)), h = juce::jmax (8, (int) (H * k));
            const auto small = img.rescaled (w, h, juce::Graphics::mediumResamplingQuality);
            const juce::Image::BitmapData px (small, juce::Image::BitmapData::readOnly);
            float br = 0, bg = 0, bb = 0; int cnt = 0;
            bool transparent = false;
            for (int x = 0; x < w; ++x)
                for (int y : { 0, h - 1 })
                {
                    const auto c = px.getPixelColour (x, y);
                    br += c.getFloatRed(); bg += c.getFloatGreen(); bb += c.getFloatBlue(); ++cnt;
                    transparent = transparent || c.getAlpha() < 20;
                }
            for (int y = 0; y < h; ++y)
                for (int x : { 0, w - 1 })
                {
                    const auto c = px.getPixelColour (x, y);
                    br += c.getFloatRed(); bg += c.getFloatGreen(); bb += c.getFloatBlue(); ++cnt;
                    transparent = transparent || c.getAlpha() < 20;
                }
            br /= (float) cnt; bg /= (float) cnt; bb /= (float) cnt;
            int x0 = w, y0 = h, x1 = -1, y1 = -1;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const auto c = px.getPixelColour (x, y);
                    const bool fg = transparent ? c.getAlpha() > 60
                                                : std::abs (c.getFloatRed() - br) + std::abs (c.getFloatGreen() - bg) + std::abs (c.getFloatBlue() - bb) > 0.18f;
                    if (! fg) continue;
                    x0 = juce::jmin (x0, x); y0 = juce::jmin (y0, y); x1 = juce::jmax (x1, x); y1 = juce::jmax (y1, y);
                }
            if (x1 < 0 || (x1 - x0) < w / 6 || (y1 - y0) < h / 6) return img.getBounds();   // sfondo non riconosciuto: foto intera
            return juce::Rectangle<int> ((int) (x0 / k), (int) (y0 / k), (int) ((x1 - x0 + 1) / k), (int) ((y1 - y0 + 1) / k))
                       .getIntersection (img.getBounds());
        }
    }

    juce::File RealPhotos::listsRoot()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("PedalTrinity").getChildFile ("RealMod");
    }

    juce::File RealPhotos::folderFor (const juce::String& list)
    {
        if (list.isEmpty() || checkListName (list).isNotEmpty())
            return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("PedalTrinity").getChildFile ("RealPhotos");
        return listsRoot().getChildFile (list);
    }

    juce::File RealPhotos::folder()
    {
        juce::SharedResourcePointer<ThemeManager> themes;
        auto list = themes->realList();
        if (list.isNotEmpty() && ! folderFor (list).isDirectory()) list = {};      // cartella sparita: lista classica
        auto dir = folderFor (list);
        if (! dir.isDirectory()) dir.createDirectory();
        static juce::StringArray listed;                 // una volta per sessione e per cartella (thread dei messaggi)
        if (! listed.contains (dir.getFullPathName())) { listed.add (dir.getFullPathName()); writeList (dir); }
        return dir;
    }

    juce::StringArray RealPhotos::lists()
    {
        juce::StringArray out;
        for (const auto& f : listsRoot().findChildFiles (juce::File::findDirectories, false))
            if (checkListName (f.getFileName()).isEmpty() && ! f.isSymbolicLink()) out.add (f.getFileName());
        out.sortNatural();
        return out;
    }

    juce::String RealPhotos::checkListName (const juce::String& name)
    {
        if (name.isEmpty() || name.length() > 40) return "il nome deve avere da 1 a 40 caratteri";
        if (name.trim() != name || name.startsWithChar ('.') || name.endsWithChar ('.')) return "il nome non puo' iniziare o finire con spazi o punti";
        for (auto c : name)
        {
            // lettere e cifre ASCII, lettere accentate latine (indipendente dalla lingua del sistema), spazio - _ .
            const bool ascii = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_' || c == '.';
            const bool latin = c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7;
            if (! (ascii || latin)) return "sono ammesse solo lettere, cifre, spazi e - _ .";
        }
        if (name.contains ("..")) return "il nome non puo' contenere \"..\"";
        static const juce::StringArray reserved { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                                  "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
        if (reserved.contains (name.upToFirstOccurrenceOf (".", false, false).toUpperCase())) return "nome riservato dal sistema";
        return {};
    }

    juce::String RealPhotos::createList (const juce::String& name, const juce::String* copyFrom)
    {
        if (auto err = checkListName (name); err.isNotEmpty()) return err;
        const auto dir = listsRoot().getChildFile (name);
        if (dir.exists()) return "esiste gia' una lista con questo nome";
        if (! dir.createDirectory()) return "impossibile creare la cartella " + dir.getFullPathName();
        if (copyFrom != nullptr)
        {
            // copia solo foto, .json e crediti (file normali, entro i limiti), mai sottocartelle o collegamenti
            const auto src = folderFor (*copyFrom);
            for (const auto& f : src.findChildFiles (juce::File::findFiles, false))
            {
                const auto ext = f.getFileExtension().toLowerCase();
                const bool photo = ext == ".jpg" || ext == ".jpeg" || ext == ".png";
                const bool text = (ext == ".json" || ext == ".txt") && f.getFileName() != "ELENCO_FOTO.txt";
                if (! (photo || text) || f.isSymbolicLink()) continue;
                if (! pt::namsafe::isSafeLocalFile (f.getFullPathName(), photo ? maxPhotoBytes : maxSidecarBytes * 16)) continue;
                f.copyFileTo (dir.getChildFile (f.getFileName()));
            }
        }
        writeList (dir);
        return {};
    }

    juce::String RealPhotos::renameList (const juce::String& from, const juce::String& to)
    {
        if (checkListName (from).isNotEmpty()) return "lista non valida";
        if (auto err = checkListName (to); err.isNotEmpty()) return err;
        const auto src = listsRoot().getChildFile (from), dst = listsRoot().getChildFile (to);
        if (! src.isDirectory()) return "lista non trovata";
        if (dst.exists() && dst != src) return "esiste gia' una lista con questo nome";
        if (! src.moveFileTo (dst)) return "impossibile rinominare la cartella";
        return {};
    }

    juce::String RealPhotos::deleteList (const juce::String& name)
    {
        if (checkListName (name).isNotEmpty()) return "lista non valida";
        const auto dir = listsRoot().getChildFile (name);
        if (! dir.isDirectory() || dir.getParentDirectory() != listsRoot()) return "lista non trovata";
        if (! dir.moveToTrash()) return "impossibile spostare la cartella nel cestino";
        return {};
    }

    void RealPhotos::writeList (const juce::File& dir)
    {
        // elenco dei nomi di file accettati, uno per pedale (riscritto se il catalogo cambia)
        const auto list = dir.getChildFile ("ELENCO_FOTO.txt");
        juce::String text;
        text << "Pedal Trinity - REAL MOD PEDALBOARD: foto personali\n"
             << "======================================================\n\n"
             << "Metti in questa cartella le TUE foto dei pedali (scattate dall'alto, pedale intero),\n"
             << "chiamate con uno dei due nomi della riga del pedale, in .jpg, .jpeg o .png.\n"
             << "Poi, nel programma: Opzioni -> Ricarica foto (con REAL MOD acceso).\n"
             << "Le foto restano sul tuo computer: usa foto tue o di cui hai i diritti.\n\n"
             << "Facoltativo, un file .json con lo stesso nome (es. ds1.json):\n"
             << "  { \"crop\": [x, y, larghezza, altezza], \"rotate\": 90, \"knobs\": false,\n"
             << "    \"name\": \"Il mio distorsore\", \"code\": \"MY-1\" }\n"
             << "crop = ritaglio in pixel, rotate = 0/90/180/270, knobs = false nasconde i pomelli 3D,\n"
             << "name / code = nome e sigla mostrati nei menu (facoltativi).\n"
             << "I pomelli si allineano sulla foto dal pannello di zoom: \"Allinea pomelli sulla foto\".\n\n"
             << "Liste: Opzioni -> REAL MOD -> Nuova lista crea altre cartelle come questa (RealMod/<nome>),\n"
             << "ognuna con le sue foto; i pedali senza foto mostrano la replica.\n\n"
             << juce::String ("NOME FILE").paddedRight (' ', 22) << juce::String ("OPPURE").paddedRight (' ', 22) << "PEDALE\n";
        for (int i = 0; i < numRealModels(); ++i)
        {
            const auto& r = realModel (i);
            text << (juce::String (r.id) + ".jpg").paddedRight (' ', 22) << (juce::String (r.code) + ".jpg").paddedRight (' ', 22)
                 << r.code << "  " << r.name << "\n";
        }
        if (! list.existsAsFile() || list.loadFileAsString() != text)
            list.replaceWithText (text);
    }

    juce::String RealPhotos::checkImageFile (const juce::File& f, int& width, int& height)
    {
        width = height = 0;
        const auto ext = f.getFileExtension().toLowerCase();
        if (ext != ".jpg" && ext != ".jpeg" && ext != ".png") return "sono ammesse solo foto .jpg, .jpeg o .png";
        if (! f.existsAsFile()) return "file non trovato";
        const auto size = f.getSize();
        if (size < 256 || size > maxPhotoBytes) return "dimensione del file non ammessa (massimo 25 MB)";
        juce::MemoryBlock head;
        {
            juce::FileInputStream in (f);
            if (! in.openedOk()) return "file non leggibile";
            in.readIntoMemoryBlock (head, juce::jmin<juce::int64> (size, 1 << 20));
        }
        const bool png = ext == ".png";
        if (! headerSize (head, png, width, height))
            return png ? "non e' un PNG valido (firma o intestazione assente)" : "non e' un JPEG valido (firma o intestazione assente)";
        if (width > maxSide || height > maxSide) return "foto troppo grande (massimo 8000 x 8000 pixel)";
        if (width < minSide || height < minSide) return "foto troppo piccola";
        return {};
    }

    juce::Image RealPhotos::compose (const juce::Image& photoIn, const ModelDef& real, juce::Rectangle<int> crop, int rotate)
    {
        constexpr float S = 2.0f;                               // composizione a 2x delle coordinate logiche
        const int W = (int) std::ceil (real.imageW * S), H = (int) std::ceil (real.imageH * S);
        juce::Image out (juce::Image::ARGB, W, H, true);
        auto photo = photoIn.getClippedImage (crop.getIntersection (photoIn.getBounds()));
        if (rotate % 360 != 0)
        {
            const bool swap = rotate == 90 || rotate == 270;
            juce::Image r (juce::Image::ARGB, swap ? photo.getHeight() : photo.getWidth(), swap ? photo.getWidth() : photo.getHeight(), true);
            juce::Graphics rg (r);
            rg.drawImageTransformed (photo, juce::AffineTransform::rotation (juce::degreesToRadians ((float) rotate),
                                                                             photo.getWidth() * 0.5f, photo.getHeight() * 0.5f)
                                                 .translated ((r.getWidth() - photo.getWidth()) * 0.5f, (r.getHeight() - photo.getHeight()) * 0.5f));
            photo = r;
        }

        auto top = real.bodyW > 0 ? juce::Rectangle<float> (real.bodyX, real.bodyY, real.bodyW, real.bodyH) * S
                                  : juce::Rectangle<float> ((float) W, (float) H).reduced (W * 0.08f, H * 0.08f);
        const float frontH = real.bodyFront > 0 ? real.bodyFront * S : top.getHeight() * 0.06f;
        const float corner = juce::jmin (top.getWidth(), top.getHeight()) * 0.045f;
        const auto front = juce::Rectangle<float> (top.getX(), top.getBottom() - corner, top.getWidth(), frontH + corner);

        juce::Graphics g (out);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        juce::Path whole;
        whole.addRoundedRectangle (top.getUnion (front), corner);
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), (int) (18 * S), { (int) (5 * S), (int) (9 * S) }).drawForPath (g, whole);

        // fronte: le ultime righe della foto, estruse e in ombra (spessore del pedale)
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path fp; fp.addRoundedRectangle (front, corner);
            g.reduceClipRegion (fp);
            const int strip = juce::jmax (2, photo.getHeight() / 30);
            g.setOpacity (1.0f);                    // drawImage usa l'opacita' corrente (l'ombra l'aveva lasciata al 60%)
            g.drawImage (photo, front.getX(), front.getY(), front.getWidth(), front.getHeight(),
                         0, photo.getHeight() - strip, photo.getWidth(), strip);
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), front.getX(), front.getY(),
                                                     juce::Colours::black.withAlpha (0.7f), front.getX(), front.getBottom(), false));
            g.fillRect (front);
        }
        // piano superiore: la foto con luce dall'alto e bordi smussati
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path tp; tp.addRoundedRectangle (top, corner);
            g.reduceClipRegion (tp);
            g.setOpacity (1.0f);
            g.drawImage (photo, top, juce::RectanglePlacement::stretchToFit);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.13f), top.getX(), top.getY(),
                                                     juce::Colours::transparentWhite, top.getX(), top.getCentreY(), false));
            g.fillRect (top);
            g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, top.getX(), top.getCentreY(),
                                                     juce::Colours::black.withAlpha (0.16f), top.getX(), top.getBottom(), false));
            g.fillRect (top);
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.22f), top.getX(), top.getY(),
                                                     juce::Colours::transparentBlack, top.getX() + top.getWidth() * 0.06f, top.getY(), false));
            g.fillRect (top.withWidth (top.getWidth() * 0.06f));
            g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, top.getRight() - top.getWidth() * 0.06f, top.getY(),
                                                     juce::Colours::black.withAlpha (0.25f), top.getRight(), top.getY(), false));
            g.fillRect (top.withLeft (top.getRight() - top.getWidth() * 0.06f));
        }
        g.setColour (juce::Colours::white.withAlpha (0.28f));
        g.drawRoundedRectangle (top.reduced (0.8f * S), corner, 0.9f * S);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (top.getUnion (front), corner, 0.8f * S);
        return out;
    }

    juce::File RealPhotos::photoFileFor (const ModelDef& real)
    {
        if (! isEnabled()) return {};
        juce::SharedResourcePointer<ThemeManager> themes;
        const auto now = juce::Time::getMillisecondCounter();
        if (! dirIndex.valid || dirIndex.list != themes->realList() || now - dirIndex.stamp > 1500)
        {
            dirIndex = {};
            dirIndex.valid = true;
            dirIndex.list = themes->realList();
            dirIndex.stamp = now;
            dirIndex.dir = folder();
            for (const auto& f : dirIndex.dir.findChildFiles (juce::File::findFiles, false))
                dirIndex.files[f.getFileName()] = f;                      // solo file della cartella, niente sottocartelle
        }
        for (auto stem : { juce::String (real.id), juce::String (real.code) })
            for (auto ext : { ".jpg", ".jpeg", ".png", ".JPG", ".JPEG", ".PNG" })
            {
                const auto it = dirIndex.files.find (stem + ext);
                if (it != dirIndex.files.end() && it->second.existsAsFile()) return it->second;
            }
        return {};
    }

    void RealPhotos::labelsFor (const ModelDef& real, juce::String& code, juce::String& name)
    {
        code = name = {};
        const auto file = photoFileFor (real);
        if (! file.existsAsFile()) return;
        const auto side = file.withFileExtension ("json");
        if (! side.existsAsFile()) return;
        const auto key = side.getFullPathName() + "|" + juce::String (side.getSize()) + "|" + juce::String (side.getLastModificationTime().toMilliseconds());
        auto& l = labels[real.id];
        if (l.key != key)
        {
            l = {};
            l.key = key;
            if (side.getSize() <= maxSidecarBytes)
            {
                const auto text = side.loadFileAsString();
                if (pt::namsafe::plausibleJson (text, (size_t) maxSidecarBytes))
                {
                    Photo p;
                    juce::Rectangle<int> crop;
                    int rot = 0;
                    bool knobs = true;
                    if (parseSidecar (juce::JSON::parse (text), { 0, 0, maxSide, maxSide }, p, crop, rot, knobs).isEmpty())
                        { l.code = p.code; l.name = p.name; }
                }
            }
        }
        code = l.code; name = l.name;
    }

    RealPhotos::Photo RealPhotos::forModel (const ModelDef& real)
    {
        if (! isEnabled()) return {};
        const auto file = photoFileFor (real);
        auto& e = cache[real.id];
        if (! file.existsAsFile()) { e = {}; return {}; }
        const auto key = file.getFullPathName() + "|" + juce::String (file.getSize()) + "|" + juce::String (file.getLastModificationTime().toMilliseconds());
        if (e.key == key) return e.photo;
        e = {};
        e.key = key;

        int w = 0, h = 0;
        if (auto err = checkImageFile (file, w, h); err.isNotEmpty()) { e.error = file.getFileName() + ": " + err; return {}; }
        const auto img = juce::ImageFileFormat::loadFrom (file);
        if (! img.isValid() || img.getWidth() != w || img.getHeight() != h) { e.error = file.getFileName() + ": immagine non decodificabile"; return {}; }

        juce::Rectangle<int> crop;
        int rotate = 0;
        bool knobs = true;
        const auto side = file.withFileExtension ("json");
        if (side.existsAsFile())
        {
            if (side.getSize() > maxSidecarBytes) { e.error = side.getFileName() + ": file troppo grande"; return {}; }
            const auto sideText = side.loadFileAsString();
            if (! pt::namsafe::plausibleJson (sideText, (size_t) maxSidecarBytes)) { e.error = side.getFileName() + ": JSON anomalo"; return {}; }
            const auto v = juce::JSON::parse (sideText);
            if (! v.isObject()) { e.error = side.getFileName() + ": JSON non valido"; return {}; }
            bool knobsFlag = true;
            if (auto err = parseSidecar (v, img.getBounds(), e.photo, crop, rotate, knobsFlag); err.isNotEmpty())
                { e.error = side.getFileName() + ": " + err; return {}; }
            knobs = knobsFlag;
        }
        if (crop.isEmpty()) crop = autoCrop (img);
        e.photo.image = compose (img, real, crop, rotate);
        e.photo.knobs = knobs;
        e.photo.file = file;
        return e.photo;
    }

    juce::String RealPhotos::parseSidecar (const juce::var& v, juce::Rectangle<int> imageBounds, Photo& photo,
                                           juce::Rectangle<int>& crop, int& rotate, bool& knobs)
    {
        if (! v.isObject()) return "JSON non valido";
        auto number = [] (const juce::var& x) { return (x.isInt() || x.isInt64() || x.isDouble()) ? (double) x : std::nan (""); };
        if (const auto* c = v.getProperty ("crop", {}).getArray(); c != nullptr)
        {
            if (c->size() != 4) return "crop deve avere 4 valori";
            int q[4];
            for (int i = 0; i < 4; ++i)
            {
                const double d = number ((*c)[i]);
                if (! std::isfinite (d) || d < 0 || d > maxSide) return "crop fuori dai limiti";
                q[i] = (int) d;
            }
            crop = juce::Rectangle<int> (q[0], q[1], q[2], q[3]).getIntersection (imageBounds);
            if (crop.getWidth() < minSide / 2 || crop.getHeight() < minSide / 2) return "crop troppo piccolo";
        }
        const double r = v.hasProperty ("rotate") ? number (v.getProperty ("rotate", 0)) : 0.0;
        if (r == 0 || r == 90 || r == 180 || r == 270) rotate = (int) r;
        else return "rotate ammette solo 0, 90, 180, 270";
        knobs = (bool) v.getProperty ("knobs", true);
        for (auto [key, maxLen, dest] : { std::tuple<const char*, int, juce::String*> { "name", 40, &photo.name },
                                          std::tuple<const char*, int, juce::String*> { "code", 16, &photo.code } })
            if (v.hasProperty (key))
            {
                const auto& x = v.getProperty (key, {});
                if (! x.isString()) return juce::String ("\"") + key + "\" deve essere un testo";
                auto text = x.toString().trim();
                if (text.length() > maxLen) return juce::String ("\"") + key + "\" troppo lungo";
                for (auto ch : text) if (ch < 32 || ch == 127) return juce::String ("\"") + key + "\" contiene caratteri non ammessi";
                *dest = text;
            }
        if (const auto* l = v.getProperty ("led", {}).getArray(); l != nullptr)
        {
            const double lu = l->size() == 2 ? number ((*l)[0]) : -1.0, lv = l->size() == 2 ? number ((*l)[1]) : -1.0;
            if (! (std::isfinite (lu) && std::isfinite (lv) && lu >= 0 && lu <= 1 && lv >= 0 && lv <= 1)) return "led fuori dai limiti";
            photo.hasLed = true; photo.ledU = (float) lu; photo.ledV = (float) lv;
        }
        if (v.hasProperty ("controls"))
        {
            auto* obj = v.getProperty ("controls", {}).getDynamicObject();
            if (obj == nullptr) return "\"controls\" deve essere un oggetto";
            const auto& props = obj->getProperties();
            if (props.size() > 32) return "troppi comandi in \"controls\"";
            for (const auto& nv : props)
            {
                const auto label = nv.name.toString();
                const auto* a = nv.value.getArray();
                if (label.isEmpty() || label.length() > 32 || a == nullptr || a->size() != 3)
                    return "\"controls\" non valido (" + label.substring (0, 32) + ")";
                float q[3];
                for (int i = 0; i < 3; ++i)
                {
                    const double d = number ((*a)[i]);
                    if (! std::isfinite (d) || d < 0.0 || d > 1.0) return "posizione fuori dai limiti (" + label + ")";
                    q[i] = (float) d;
                }
                if (q[2] <= 0.005f || q[2] > 0.5f) return "raggio non valido (" + label + ")";
                photo.controls.push_back ({ label, PhotoKnob { q[0], q[1], q[2] } });
            }
        }
        return {};
    }

    juce::String RealPhotos::saveAlignment (const juce::File& photoFile, const std::vector<std::pair<juce::String, PhotoKnob>>& knobs,
                                            bool hasLed, float ledU, float ledV)
    {
        // solo accanto a una foto della cartella RealPhotos; conserva crop e rotate gia' presenti
        if (photoFile.getParentDirectory() != folder()) return "la foto non e' nella cartella della lista REAL MOD in uso";
        const auto side = photoFile.withFileExtension ("json");
        juce::var root;
        if (side.existsAsFile() && side.getSize() <= maxSidecarBytes)
        {
            const auto text = side.loadFileAsString();
            if (pt::namsafe::plausibleJson (text, (size_t) maxSidecarBytes)) root = juce::JSON::parse (text);
        }
        auto* obj = root.getDynamicObject();
        if (obj == nullptr) { obj = new juce::DynamicObject(); root = juce::var (obj); }
        auto* ctr = new juce::DynamicObject();
        auto clamp01 = [] (float x) { return juce::jlimit (0.0, 1.0, std::round ((double) x * 10000.0) / 10000.0); };
        for (const auto& k : knobs)
        {
            juce::Array<juce::var> a { clamp01 (k.second.x), clamp01 (k.second.y), juce::jlimit (0.006, 0.5, clamp01 (k.second.z)) };
            ctr->setProperty (k.first, a);
        }
        obj->setProperty ("knobs", true);
        obj->setProperty ("controls", juce::var (ctr));
        if (hasLed) obj->setProperty ("led", juce::Array<juce::var> { clamp01 (ledU), clamp01 (ledV) });
        if (! side.replaceWithText (juce::JSON::toString (root, false))) return "impossibile scrivere " + side.getFileName();
        return {};
    }

    juce::String RealPhotos::problemFor (const char* id) const
    {
        const auto it = cache.find (id);
        return it != cache.end() ? it->second.error : juce::String();
    }

    void RealPhotos::reload() { cache.clear(); labels.clear(); dirIndex = {}; }
}
