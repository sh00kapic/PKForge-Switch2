/*
 *   This file is part of PKSM-Core
 *   Copyright (C) 2016-2022 Bernardo Giordano, Admiral Fish, piepie62
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
 *       * Requiring preservation of specified reasonable legal notices or
 *         author attributions in that material or in the Appropriate Legal
 *         Notices displayed by works containing it.
 *       * Prohibiting misrepresentation of the origin of that material,
 *         or requiring that modified versions of such material be marked in
 *         reasonable ways as different from the original version.
 */

#include "utils/ValueConverter.hpp"
#include "enums/Species.hpp"
#include "g1values.hpp"
#include "g2values.hpp"
#include "g3values.hpp"
#include <algorithm>
#include <array>

namespace pksm
{
    namespace
    {
        constexpr int FirstUnaligned9 = 917;
        // Paldea's stored species order, from PKHeX SpeciesConverter (GPL-3.0).
        constexpr std::array<s8, 109> NationalToInternal9 = {
            1, 1, 1, 1, 33, 33, 33, 21, 21, 44, 44, 7, 7, 7,
            29, 31, 31, 31, 68, 68, 68, 2, 2, 17, 17, 30, 30, 24,
            24, 28, 28, 58, 58, 12, -13, -13, -31, -31, -29, -29, 43, 43,
            43, -31, -31, -3, -30, -30, -23, -23, -14, -24, -3, -3, -47, -47,
            -12, -27, -27, -44, -46, -26, 31, 29, -53, -65, 25, -6, -3, -7,
            -4, -4, -8, -4, 1, -3, -3, -6, -4, -47, -47, -47, -23, -23,
            -5, -7, -9, -7, -20, -13, -9, -9, -29, -23, 1, 12, 12, 0,
            0, 0, -6, 5, -6, -3, -3, -2, -4, -3, -3,
        };
        constexpr auto InternalToNational9 = []
        {
            std::array<s8, NationalToInternal9.size()> offsets{};
            for (int i = 0; i < static_cast<int>(offsets.size()); ++i)
                offsets[i + NationalToInternal9[i]] = -NationalToInternal9[i];
            return offsets;
        }();

        u16 shiftSpecies9(u16 id, const std::array<s8, 109>& offsets)
        {
            const int index = id - FirstUnaligned9;
            return index >= 0 && index < static_cast<int>(offsets.size()) ? id + offsets[index] : id;
        }
    }

    Species SpeciesConverter::g9ToNational(u16 v)
    {
        return Species{shiftSpecies9(v, InternalToNational9)};
    }

    u16 SpeciesConverter::nationalToG9(Species v)
    {
        return shiftSpecies9(u16(v), NationalToInternal9);
    }

    Species SpeciesConverter::g1ToNational(u8 v)
    {
        if (v < internal::g1ToSpecies.size())
        {
            return Species{internal::g1ToSpecies[v]};
        }
        return Species::None;
    }

    u8 SpeciesConverter::nationalToG1(Species v)
    {
        if (u16(v) < internal::speciesToG1.size())
        {
            return internal::speciesToG1[u16(v)];
        }
        return 0;
    }

    Species SpeciesConverter::g3ToNational(u16 v)
    {
        if (v < internal::g3ToSpecies.size())
        {
            return Species{internal::g3ToSpecies[v]};
        }
        return Species::None;
    }

    u16 SpeciesConverter::nationalToG3(Species v)
    {
        if (u16(v) < internal::speciesToG3.size())
        {
            return internal::speciesToG3[u16(v)];
        }
        return 0;
    }

    u16 ItemConverter::g1ToNational(u8 v)
    {
        //"v < internal::g1ToItem.size()" is always true due to a size of 256
        return internal::g1ToItem[v];
    }

    u8 ItemConverter::nationalToG1(u16 v)
    {
        if (v == 0)
        {
            return 0;
        }

        auto it = std::find(internal::g1ToItem.begin(), internal::g1ToItem.end(), v);
        if (it == internal::g1ToItem.end())
        {
            return 0;
        }
        return std::distance(internal::g1ToItem.begin(), it);
    }

    u16 ItemConverter::g2ToNational(u8 v)
    {
        //"v < internal::g2ToItem.size()" is always true due to a size of 256
        return internal::g2ToItem[v];
    }

    u8 ItemConverter::nationalToG2(u16 v)
    {
        if (v == 0)
        {
            return 0;
        }

        auto it = std::find(internal::g2ToItem.begin(), internal::g2ToItem.end(), v);
        if (it == internal::g2ToItem.end())
        {
            return 0;
        }
        return std::distance(internal::g2ToItem.begin(), it);
    }

    u16 ItemConverter::g3ToNational(u16 v)
    {
        if (v < internal::g3ToItem.size())
        {
            return internal::g3ToItem[v];
        }
        return 0;
    }

    u16 ItemConverter::nationalToG3(u16 v)
    {
        if (v == 0)
        {
            return 0;
        }

        auto it = std::find(internal::g3ToItem.begin(), internal::g3ToItem.end(), v);
        if (it == internal::g3ToItem.end())
        {
            return 0;
        }
        return std::distance(internal::g3ToItem.begin(), it);
    }
}
