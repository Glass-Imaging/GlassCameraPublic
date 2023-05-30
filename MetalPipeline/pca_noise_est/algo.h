/*
 *
 * This file is part of the PCA Noise Estimation algorithm.
 *
 * Copyright(c) 2013 Miguel Colom.
 * miguel.colom@cmla.ens-cachan.fr
 *
 * This file may be licensed under the terms of of the
 * GNU General Public License Version 2 (the ``GPL'').
 *
 * Software distributed under the License is distributed
 * on an ``AS IS'' basis, WITHOUT WARRANTY OF ANY KIND, either
 * express or implied. See the GPL for the specific language
 * governing rights and limitations.
 *
 * You should have received a copy of the GPL along with this
 * program. If not, go to http://www.gnu.org/licenses/gpl.html
 * or write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifndef _ALGO_H_
#define _ALGO_H_

#include "framework/CFramework.h"

#include "gls_image.hpp"

//! Main algorithm entry point
/*!
  \param argc Number of arguments passed to the program
  \param argv Array of argc arguments passed to the program
*/
void algorithm(int argc, char** argv);

void algorithm(const gls::image<gls::luma_pixel_float>& image);

#endif
