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

#ifndef SERVICEMETATYPES_H
#define SERVICEMETATYPES_H

// Registers the Qt metatypes carried by the typed request ready()/statusChanged()
// signals and exposed to QML. Call once at startup (before any model/engine use).
void registerMeeTubeMetaTypes();

#endif // SERVICEMETATYPES_H
