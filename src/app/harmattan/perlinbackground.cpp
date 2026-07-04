/*
 * Copyright (C) 2016 Stuart Howarth <showarth@marxoft.co.uk>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "harmattan/perlinbackground.h"
#include <QPainter>
#include <algorithm>
#include <cstdlib>

// Resolution of the noise grid that gets upscaled to the item size. Kept tiny on
// purpose: the smooth upscale dominates on the N9's Cortex-A8, so we sample noise
// cheaply and let the bilinear filter produce the soft look.
static const int RES = 16;

// How many noise "units" advance per second (tuned so motion speed is independent
// of the timer interval below), scaled by `speed`.
static const double TIME_PER_SEC = 0.375;

// MeeTube brand palette: deep red -> brand dark (#B40D0D) -> mid (#F11B1B) ->
// light orange-red (#FF5A36). The pow() shaping in renderNoise() keeps the field
// mostly dark red with occasional bright flecks, so white header text stays legible.
static const QColor PALETTE_BRAND[4] = {
    QColor(122, 10, 10),
    QColor(180, 13, 13),
    QColor(241, 27, 27),
    QColor(255, 90, 54),
};

PerlinBackground::PerlinBackground(QDeclarativeItem *parent)
    : QDeclarativeItem(parent)
    , m_speed(0.15)
    , m_time(0.0)
    , m_noise(RES, RES, QImage::Format_RGB32)
{
    setFlag(QGraphicsItem::ItemHasNoContents, false);

    // Fixed seed: the permutation only needs to look random, and a constant seed
    // keeps the pattern reproducible and avoids pulling in <ctime>.
    std::srand(1337);
    initPermutation();

    for (int i = 0; i < 4; i++)
        m_palette[i] = PALETTE_BRAND[i];

    renderNoise(); // make sure the grid has content before the first paint

    m_timer = new QTimer(this);
    m_timer->setInterval(80); // ~12.5 fps: plenty for an ambient background
    connect(m_timer, SIGNAL(timeout()), this, SLOT(tick()));
    m_timer->start();
}

QVariant PerlinBackground::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == QGraphicsItem::ItemVisibleHasChanged) {
        // Don't burn CPU animating a hidden banner (e.g. the collapsed header).
        if (value.toBool())
            m_timer->start();
        else
            m_timer->stop();
    }
    return QDeclarativeItem::itemChange(change, value);
}

void PerlinBackground::initPermutation()
{
    int p[256];
    for (int i = 0; i < 256; i++) p[i] = i;
    for (int i = 255; i > 0; i--) {
        int j = std::rand() % (i + 1);
        std::swap(p[i], p[j]);
    }
    for (int i = 0; i < 512; i++)
        m_perm[i] = p[i & 255];
}

double PerlinBackground::grad(int hash, double x, double y, double z)
{
    int h = hash & 15;
    double u = h < 8 ? x : y;
    double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

double PerlinBackground::noise(double x, double y, double z)
{
    int X = (int)std::floor(x) & 255;
    int Y = (int)std::floor(y) & 255;
    int Z = (int)std::floor(z) & 255;

    x -= std::floor(x);
    y -= std::floor(y);
    z -= std::floor(z);

    double u = fade(x), v = fade(y), w = fade(z);

    int A  = m_perm[X]   + Y, AA = m_perm[A] + Z, AB = m_perm[A+1] + Z;
    int B  = m_perm[X+1] + Y, BA = m_perm[B] + Z, BB = m_perm[B+1] + Z;

    return lerp(
        lerp(lerp(grad(m_perm[AA],   x,   y,   z),
                  grad(m_perm[BA],   x-1, y,   z), u),
             lerp(grad(m_perm[AB],   x,   y-1, z),
                  grad(m_perm[BB],   x-1, y-1, z), u), v),
        lerp(lerp(grad(m_perm[AA+1], x,   y,   z-1),
                  grad(m_perm[BA+1], x-1, y,   z-1), u),
             lerp(grad(m_perm[AB+1], x,   y-1, z-1),
                  grad(m_perm[BB+1], x-1, y-1, z-1), u), v), w);
}

QColor PerlinBackground::lerpColor(const QColor &a, const QColor &b, double t)
{
    return QColor(
        a.red()   + (b.red()   - a.red())   * t,
        a.green() + (b.green() - a.green()) * t,
        a.blue()  + (b.blue()  - a.blue())  * t
    );
}

// Fill the small noise grid for the current m_time. Cheap: RES*RES samples written
// straight to the scanlines (no per-pixel setPixel overhead).
void PerlinBackground::renderNoise()
{
    const double SCALE = 0.75;
    const double tx = m_time * 0.5;
    const double ty = m_time * 0.35;
    const double tz = m_time * 0.2;

    for (int py = 0; py < RES; py++) {
        QRgb *line = reinterpret_cast<QRgb *>(m_noise.scanLine(py));
        double ny = py * (SCALE / RES);
        for (int px = 0; px < RES; px++) {
            double nx = px * (SCALE / RES);

            double n = noise(nx + tx, ny + ty, tz);

            n = (n + 1.0) * 0.5;
            n = std::pow(n, 3.5);

            int    idx = (int)(n * 2.99);
            double t   = n * 3.0 - idx;

            QColor c = lerpColor(m_palette[idx & 3], m_palette[(idx + 1) & 3], t);
            line[px] = c.rgb();
        }
    }
}

// Upscale the grid into a server-side pixmap once. paint() then just blits it, so
// incidental repaints (and the X11 transport) never redo the smooth scale.
void PerlinBackground::rescale()
{
    int w = (int)width();
    int h = (int)height();
    if (w <= 0 || h <= 0) {
        m_frame = QPixmap();
        m_frameSize = QSize();
        return;
    }

    m_frame = QPixmap::fromImage(
        m_noise.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    m_frameSize = QSize(w, h);
}

void PerlinBackground::setSpeed(double v)
{
    if (v < 0.0) v = 0.0;
    if (m_speed == v) return;
    m_speed = v;
    emit speedChanged();
}

void PerlinBackground::tick()
{
    const double dt = m_timer->interval() / 1000.0;
    m_time += m_speed * TIME_PER_SEC * dt;

    // Produce the next frame off the paint path so paint() stays a pure blit.
    renderNoise();
    rescale();
    update();
}

void PerlinBackground::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    int w = (int)width();
    int h = (int)height();
    if (w <= 0 || h <= 0)
        return;

    // Only the very first paint or a geometry change costs a rescale here;
    // animation frames already prepared m_frame in tick().
    if (m_frame.isNull() || m_frameSize != QSize(w, h))
        rescale();

    painter->drawPixmap(0, 0, m_frame);
}
