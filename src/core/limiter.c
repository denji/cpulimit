/**
 * cpulimit - a CPU limiter for Linux
 *
 * Copyright (C) 2005-2012, by:  Angelo Marletta <angelo dot marletta at gmail dot com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "core/limiter.h"

void compute_slices(double limit, double pcpu, double prev_rate, long chunk_us, struct slices *out) {
    const int64_t chunk_ns = (int64_t)chunk_us * 1000;
    double rate;
    if (pcpu < 0) {
        //it's the 1st cycle: start from the requested limit
        rate = limit;
    } else {
        //scale the previous rate by how far the measured usage is from the limit:
        //rate = prev_rate * limit / pcpu, which is clamped to 1 below. The division happens only
        //when it is a real quotient below 1: an idle process (pcpu == 0), the denormal usage it
        //decays to, and a NaN measurement all take the "run fully" side, so no NaN or infinity is
        //ever produced (the form "target >= pcpu ? 1 : target / pcpu" divided 0 by a NaN pcpu).
        double target = prev_rate * limit;
        rate = (target < pcpu) ? target / pcpu : 1;
    }
    if (rate != rate) {
        rate = 1;  //NaN
    }
    if (rate < 0) {
        rate = 0;
    }
    if (rate > 1) {
        rate = 1;
    }
    out->work_ns = (int64_t)((double)chunk_ns * rate);
    if (out->work_ns > chunk_ns) {
        out->work_ns = chunk_ns;
    }
    out->stop_ns = chunk_ns - out->work_ns;
    out->rate = rate;
}
