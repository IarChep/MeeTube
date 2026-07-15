/****************************************************************************
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the Qt Components project.
**
** $QT_BEGIN_LICENSE:BSD$
** You may use this file under the terms of the BSD license as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of Nokia Corporation and its Subsidiary(-ies) nor
**     the names of its contributors may be used to endorse or promote
**     products derived from this software without specific prior written
**     permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
** $QT_END_LICENSE$
**
****************************************************************************/

.pragma library

var FONT_FAMILY = "Nokia Pure Text";
var FONT_FAMILY_LIGHT = "Nokia Pure Text Light"

// we use a fallback font when language is set to farsi
var FONT_FAMILY_FARSI = "Arial"
var FONT_FAMILY_LIGHT_FARSI = "Arial"

var FONT_DEFAULT_SIZE = 24; // DEPRECATED

var FONT_XLARGE  = 32;
var FONT_LARGE   = 28;
var FONT_SLARGE  = 26;
var FONT_DEFAULT = 24;
var FONT_LSMALL  = 22; 
var FONT_SMALL   = 20;
var FONT_XSMALL  = 18;
var FONT_XXSMALL = 16;

var COLOR_FOREGROUND = "#191919"; // Text color
var COLOR_SECONDARY_FOREGROUND = "#a6a8ab"; // Secondary text
var COLOR_BACKGROUND = "#E0E1E2"; // Background
var COLOR_SELECT = "#4591ff"; //Selected item background

var COLOR_INVERTED_FOREGROUND = "#ffffff"; // Text color
var COLOR_INVERTED_SECONDARY_FOREGROUND = "#8c8c8c"; // Secondary text
var COLOR_INVERTED_BACKGROUND = "#000000"; // Background

var COLOR_DISABLED_FOREGROUND = "#b2b2b4";

var COLOR_BUTTON_FOREGROUND            = "#000000" //text color
var COLOR_BUTTON_INVERTED_FOREGROUND   = "#ffffff" //inverted text color
var COLOR_BUTTON_SECONDARY_FOREGROUND  = "#8c8c8c" //secondary text
var COLOR_BUTTON_DISABLED_FOREGROUND   = "#B2B2B4" //disabled text
var COLOR_BUTTON_BACKGROUND            = "#000000" //background

var SIZE_ICON_DEFAULT = 32;
var SIZE_ICON_LARGE = 48;

var CORNER_MARGINS = 22;

var MARGIN_DEFAULT = 0;
var MARGIN_XLARGE = 16;

// Distance in pixels from the widget bounding box inside which a release
// event would still be accepted and trigger the widget
var RELEASE_MISS_DELTA = 30;

var OPACITY_ENABLED = 1.0;
var OPACITY_DISABLED = 0.5;
var SIZE_BUTTON = 64;

var PADDING_XSMALL  = 2;
var PADDING_SMALL   = 4;
var PADDING_MEDIUM  = 6;
var PADDING_LARGE   = 8;
var PADDING_DOUBLE  = 12;
var PADDING_XLARGE  = 16;
var PADDING_XXLARGE = 24;

var SCROLLDECORATOR_SHORT_MARGIN = 8;
var SCROLLDECORATOR_LONG_MARGIN = 4;

var TOUCH_EXPANSION_MARGIN = -12;

var BUTTON_WIDTH = 322;
var BUTTON_HEIGHT = 51;
var BUTTON_LABEL_MARGIN = 10;

var FIELD_DEFAULT_HEIGHT = 52;

//Common UI layouts
var DEFAULT_MARGIN = 16;
var BUTTON_SPACING = 6;
var HEADER_DEFAULT_HEIGHT_PORTRAIT = 72;
var HEADER_DEFAULT_HEIGHT_LANDSCAPE = 46;
var HEADER_DEFAULT_TOP_SPACING_PORTRAIT = 20;
var HEADER_DEFAULT_BOTTOM_SPACING_PORTRAIT = 20;
var HEADER_DEFAULT_TOP_SPACING_LANDSCAPE = 16;
var HEADER_DEFAULT_BOTTOM_SPACING_LANDSCAPE = 14;
var LIST_ITEM_HEIGHT_SMALL = 64;
var LIST_ITEM_HEIGHT_DEFAULT = 80;

// ---------------------------------------------------------------------------
// MeeTube additions (NOT part of the platform UIConstants mirror above).
// Brand + semantic colors and animation durations, centralized here so the
// QML never hardcodes them (the validator flags hardcoded colors).
// ---------------------------------------------------------------------------
var COLOR_BRAND_RED       = "#F11B1B"; // YouTube-style brand red
var COLOR_YOUTUBE_RED     = "#CC0000"; // deep, saturated YouTube red (selected category text)
var COLOR_SCRIM           = "#cc000000"; // dark overlay over thumbnails (duration badge, play scrim)
var COLOR_SCRIM_LIGHT     = "#66000000"; // lighter overlay (hover/press over imagery)
var COLOR_DIVIDER         = "#333333"; // 1px hairline (subtle on the dark theme)

// Animation durations (ms) — not defined by the platform UIConstants; centralized.
var ANIM_FAST    = 150;
var ANIM_DEFAULT = 250;
var ANIM_SLOW    = 400;

// Player overlay — metrics + colors lifted from the N9 stock video player
// (video-suite / libvsvideowidget: screenshots + theme CSS, 2026-07-15).
var SIZE_PLAYER_BAR       = 56;        // bottom control bar height
var SIZE_PLAYER_ROUNDBTN  = 40;        // round back button diameter
var SIZE_PLAYER_GLYPH     = 20;        // icon scaled into the round button
var SIZE_SEEK_TRACK       = 8;         // scrubber line thickness
var SIZE_SEEK_HANDLE      = 12;        // small square scrub handle
var COLOR_SEEK_ELAPSED    = "#8314AC"; // color11 purple — stock elapsed fill
var COLOR_SEEK_TRACK      = "#292829"; // stock track gray
var COLOR_SEEK_HANDLE     = "#C7C7C7"; // handle light gray
var COLOR_TIME_LABEL      = "#797979"; // MSliderMinMaxLabel (stock video.css)
var COLOR_PLAYER_BUTTON   = "#1C1C1C"; // round button fill
var COLOR_PLAYER_BUTTON_RING = "#3A3A3A"; // its thin border


