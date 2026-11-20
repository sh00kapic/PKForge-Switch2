// SPDX-License-Identifier: GPL-3.0-or-later
using PKHeX.Core;

namespace PKForge.Engine;

/// <summary>The Route 5 and Bridge Field nurseries share a block with independent parents and seeds.</summary>
internal sealed class GalarNursery(Daycare8 block, int facility) : IDaycareStorage, IDaycareRandomState<ulong>
{
    static readonly int StoredSize = new PK8().SIZE_STORED;
    public int DaycareSlotCount => 2;
    public Memory<byte> GetDaycareSlot(int index) => block[facility * 2 + index][..StoredSize];
    public bool IsDaycareOccupied(int index) => facility == 0 ? block.GetDaycare1SlotOccupied(index) : block.GetDaycare2SlotOccupied(index);
    public void SetDaycareOccupied(int index, bool occupied) =>
        block.Data[Daycare8.GetDaycareSlotOffset(facility, index) - 1] = occupied ? (byte)1 : (byte)0;
    public ulong Seed { get => block.GetDaycareSeed(facility); set => block.SetDaycareSeed(facility, value); }
}
