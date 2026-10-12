/*
	This file is part of reicast.

    reicast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    reicast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with reicast.  If not, see <https://www.gnu.org/licenses/>.

    Some PLFO and FEG code from Highly_Theoretical lib by Neill Corlett
    (https://gitlab.com/kode54/highly_theoretical)
 */

/* Flycast/reicast generic hardware model, GPL-2.0-or-later.
 * Upstream revision 149e651d24b3e3b3ddeff1d022f47813c40e9fc5.
 * https://github.com/flyinghead/flycast
 * See tools/portable/notices/Flycast-GPL-2.0.txt and
 * tools/portable/notices/Flycast-hardware.txt. No game-disc content.
 *
 * JGR Portable modification, 2026-10-11: re-extracted AEG_Attack_Time and
 * AEG_DSR_Time from core/hw/aica/sgc_if.cpp as static const arrays,
 * preserving their values and upstream license notice; added this provenance notice.
 */

static const double AEG_Attack_Time[64] =
{
	-1,-1,8100.0,6900.0,6000.0,4800.0,4000.0,3400.0,3000.0,2400.0,2000.0,1700.0,1500.0,
	1200.0,1000.0,860.0,760.0,600.0,500.0,430.0,380.0,300.0,250.0,220.0,190.0,150.0,130.0,110.0,95.0,
	76.0,63.0,55.0,47.0,38.0,31.0,27.0,24.0,19.0,15.0,13.0,12.0,9.4,7.9,6.8,6.0,4.7,3.8,3.4,3.0,2.4,
	2.0,1.8,1.6,1.3,1.1,0.93,0.85,0.65,0.53,0.44,0.40,0.35,0.0,0.0
};

static const double AEG_DSR_Time[64] =
{
	-1,-1,118200.0,101300.0,88600.0,70900.0,59100.0,50700.0,44300.0,35500.0,29600.0,25300.0,22200.0,17700.0,
	14800.0,12700.0,11100.0,8900.0,7400.0,6300.0,5500.0,4400.0,3700.0,3200.0,2800.0,2200.0,1800.0,1600.0,1400.0,1100.0,
	920.0,790.0,690.0,550.0,460.0,390.0,340.0,270.0,230.0,200.0,170.0,140.0,110.0,98.0,85.0,68.0,57.0,49.0,43.0,34.0,
	28.0,25.0,22.0,18.0,14.0,12.0,11.0,8.5,7.1,6.1,5.4,4.3,3.6,3.1
};
