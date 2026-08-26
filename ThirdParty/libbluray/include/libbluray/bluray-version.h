/*
 * This file is part of libbluray
 * Copyright (C) 2013  Petri Hintukainen <phintuka@users.sourceforge.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _BLURAY_VERSION_H_
#define _BLURAY_VERSION_H_

#define BLURAY_VERSION_MAJOR 1
#define BLURAY_VERSION_MINOR 4
#define BLURAY_VERSION_MICRO 0

#define BLURAY_VERSION_CODE \
  ((BLURAY_VERSION_MAJOR * 10000) + \
   (BLURAY_VERSION_MINOR * 100) + \
   BLURAY_VERSION_MICRO)

#define BLURAY_VERSION_STRING "1.4.0"

#endif /* _BLURAY_VERSION_H_ */
