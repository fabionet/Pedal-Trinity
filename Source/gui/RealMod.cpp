/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "RealMod.h"
#include "Theme.h"
#include <cstring>

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

    juce::File RealPhotos::folder()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("PedalTrinity").getChildFile ("RealPhotos");
        if (! dir.isDirectory()) dir.createDirectory();
        return dir;
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

    RealPhotos::Photo RealPhotos::forModel (const ModelDef& real)
    {
        const auto dir = folder();
        juce::File file;
        for (auto stem : { juce::String (real.id), juce::String (real.code) })
            for (auto ext : { ".jpg", ".jpeg", ".png", ".JPG", ".JPEG", ".PNG" })
                if (! file.existsAsFile())
                {
                    const auto f = dir.getChildFile (stem + ext);
                    if (f.existsAsFile() && f.getParentDirectory() == dir) file = f;     // niente percorsi fuori cartella
                }
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
            const auto v = juce::JSON::parse (side.loadFileAsString());
            if (! v.isObject()) { e.error = side.getFileName() + ": JSON non valido"; return {}; }
            if (const auto* c = v.getProperty ("crop", {}).getArray(); c != nullptr && c->size() == 4)
            {
                int q[4];
                for (int i = 0; i < 4; ++i)
                {
                    const auto& x = (*c)[i];
                    if (! (x.isInt() || x.isInt64() || x.isDouble())) { e.error = side.getFileName() + ": crop non numerico"; return {}; }
                    const double d = (double) x;
                    if (! std::isfinite (d) || d < 0 || d > maxSide) { e.error = side.getFileName() + ": crop fuori dai limiti"; return {}; }
                    q[i] = (int) d;
                }
                crop = juce::Rectangle<int> (q[0], q[1], q[2], q[3]).getIntersection (img.getBounds());
                if (crop.getWidth() < minSide / 2 || crop.getHeight() < minSide / 2) { e.error = side.getFileName() + ": crop troppo piccolo"; return {}; }
            }
            const int r = (int) v.getProperty ("rotate", 0);
            if (r == 0 || r == 90 || r == 180 || r == 270) rotate = r;
            else { e.error = side.getFileName() + ": rotate ammette solo 0, 90, 180, 270"; return {}; }
            knobs = (bool) v.getProperty ("knobs", true);
        }
        if (crop.isEmpty()) crop = autoCrop (img);
        e.photo.image = compose (img, real, crop, rotate);
        e.photo.knobs = knobs;
        e.photo.file = file;
        return e.photo;
    }

    juce::String RealPhotos::problemFor (const char* id) const
    {
        const auto it = cache.find (id);
        return it != cache.end() ? it->second.error : juce::String();
    }

    void RealPhotos::reload() { cache.clear(); }
}
