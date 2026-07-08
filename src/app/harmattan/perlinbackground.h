/*
 * Copyright (C) 2026 IarChep
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

// Animated Perlin-noise brand banner — a QDeclarativeItem that renders a slowly
// drifting field in MeeTube's red palette. Ported from throne-harmattan's
// PerlinNoiseBackground; MeeTube uses it as the global HeaderBar background so the
// brand chrome is subtly alive. Registered as MeeTube 1.0 PerlinBackground.
#ifndef PERLINBACKGROUND_H
#define PERLINBACKGROUND_H

#include <QDeclarativeItem>
#include <QPainter>
#include <QTimer>
#include <QImage>
#include <QPixmap>
#include <QColor>
#include <QSize>
#include <cmath>

class PerlinBackground : public QDeclarativeItem
{
    Q_OBJECT
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)

public:
    explicit PerlinBackground(QDeclarativeItem *parent = 0);

    double speed() const { return m_speed; }
    void setSpeed(double v);

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *);

signals:
    void speedChanged();

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value);

private slots:
    void tick();

private:
    void initPermutation();
    double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    double lerp(double a, double b, double t) { return a + t * (b - a); }
    double grad(int hash, double x, double y, double z);
    double noise(double x, double y, double z);
    QColor lerpColor(const QColor &a, const QColor &b, double t);

    void renderNoise();   // refill the small RES*RES grid from the current time
    void rescale();       // upscale the grid into m_frame at the current size

    double  m_speed;
    double  m_time;
    int     m_perm[512];

    QColor  m_palette[4];

    QImage  m_noise;       // small noise grid, allocated once and reused every frame
    QPixmap m_frame;       // upscaled frame paint() blits; rebuilt only on tick or resize
    QSize   m_frameSize;   // size m_frame was last scaled to (detects geometry changes)

    QTimer *m_timer;
};

#endif
