/*
 * purple - Xfire Protocol Plugin
 *
 * This file is part of Gfire.
 *
 * See the AUTHORS file distributed with Gfire for a full list of
 * all contributors and this files copyright holders.
 *
 * Gfire is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Gfire.  If not, see <http://www.gnu.org/licenses/>.
*/

#define FIXME(s) printf("FIXME:STUB:%s\n", s)

/* defines for purple_debug, so that we never pass a null string pointer */
#define NN(sptr) (sptr ? sptr : "{NULL}")

/* define for a binary string, so we never pass a bin array element or
 * deference a null binary string to purple_debug
*/
#define NNA(ba, bai) (ba ? bai : 0x00)
