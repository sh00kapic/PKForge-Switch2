// SPDX-License-Identifier: GPL-3.0-or-later
using PKForge.Domain;
using PKForge.Engine;
using PKHeX.Core;

sealed partial class Workbench
{
    bool Breeding()
    {
        switch (command)
        {
            case "nursery":
                var nursery = session.GetDaycare();
                if (!nursery.Supported) Info("No accessible Day Care facility in this format.");
                foreach (var (facility,index) in nursery.Facilities.Select((f,i)=>(f,i)))
                {
                    var storage = session.GetDaycareFacilities()[index];
                    Info(facility.Name,storage is IDaycareEggState ? (facility.EggAvailable?"Egg available":"No pending egg") : "");
                    var seed = storage switch { IDaycareRandomState<ulong> u => u.Seed.ToString("X16"),
                        IDaycareRandomState<uint> u => u.Seed.ToString("X8"), IDaycareRandomState<ushort> u => u.Seed.ToString("X4"),
                        IDaycareRandomState<UInt128> u => u.Seed.ToString("X32"), _ => null };
                    if (seed!=null) Edit("seed/"+index,"Nursery RNG seed (hex)",seed);
                    foreach (var p in facility.Slots)
                    {
                        if (p.Occupied) Action($"withdraw/{index}/{p.Index}","Withdraw "+p.SpeciesName,$"Lv {p.Level}  {p.Nickname}  EXP {p.Experience}");
                        else Info($"Parent slot {p.Index+1}","Empty");
                    }
                }
                if (EggService.IsSupported(session))
                {
                    Edit("factory","Create egg by species","Species name or ID");
                    Edit("factoryshiny","Create shiny egg","Species name or ID");
                    Edit("factoryiv","Create perfect-IV egg","Species name or ID");
                }
                Link("party","Select party parent or egg"); Link("boxes","Select boxed parent or egg"); break;
            case "seed":
                var seedFacility=session.GetDaycareFacilities()[Index()];
                const System.Globalization.NumberStyles hex=System.Globalization.NumberStyles.AllowHexSpecifier;
                switch (seedFacility) {
                    case IDaycareRandomState<ushort> u: u.Seed=ushort.Parse(input,hex); break;
                    case IDaycareRandomState<uint> u: u.Seed=uint.Parse(input,hex); break;
                    case IDaycareRandomState<ulong> u: u.Seed=ulong.Parse(input,hex); break;
                    case IDaycareRandomState<UInt128> u: u.Seed=UInt128.Parse(input,hex); break;
                    default: throw new NotSupportedException("This facility does not store an editable seed.");
                }
                Finish("Nursery RNG seed updated."); break;
            case "depositparent":
                var source = Entity;
                if (source.Species==0 || source.IsEgg || (box==-1 && Save.PartyCount<=1) ||
                    (box>=0 && Save.IsBoxSlotOverwriteProtected(box,slot))) throw new InvalidOperationException("Choose an unprotected parent and keep one party member.");
                var storageSlot=session.GetDaycareFacilities()[Index()];
                if (storageSlot.IsDaycareOccupied(Index(2)) || storageSlot is IDaycareEggState { IsEggAvailable:true })
                    throw new InvalidOperationException("Collect the waiting egg or choose an empty parent slot.");
                var buffer=storageSlot.GetDaycareSlot(Index(2)).Span;
                if (Save is SAV5 or SAV8BS) Save.SetSlotFormatParty(source,buffer,EntityImportSettings.None);
                else Save.SetSlotFormatStored(source,buffer,EntityImportSettings.None);
                storageSlot.SetDaycareOccupied(Index(2),true);
                if (storageSlot is IDaycareExperience xp) xp.SetDaycareEXP(Index(2),0);
                if (!storageSlot.IsDaycareOccupied(Index(2)) || Save.GetStoredSlot(buffer).Species!=source.Species)
                    throw new InvalidDataException("The Day Care did not retain the parent.");
                session.ReleaseSlot(box,slot); Finish("Parent moved into the Day Care."); break;
            case "withdraw":
                if (session.GetDaycareFacilities()[Index()] is IDaycareEggState { IsEggAvailable:true })
                    throw new InvalidOperationException("Collect the waiting egg before withdrawing a parent.");
                var w = session.WithdrawDaycareToFirstEmptyBox(Index(),Index(2)); Finish($"{w.SpeciesName} moved to box {w.Box+1}, slot {w.Slot+1}."); break;
            case "factory": case "factoryshiny": case "factoryiv":
                var spec = SpeciesForm();
                if (spec.Form!=0) throw new ArgumentException("The egg factory accepts a species. Use encounter generation to select a form.");
                var eggTarget=Empty(); Info("Destination",$"Box {eggTarget.Box+1}, slot {eggTarget.Slot+1}");
                Finish(generator.GenerateEggs(session,[spec.Species],new(command=="factoryiv",command=="factoryshiny"))); break;
            case "egg":
                var parent = Entity;
                var rules = ParkDaycareService.RulesFor(parent);
                if (Save.Generation >= 3 && !parent.IsEgg)
                    foreach (var (f,i) in session.GetDaycare().Facilities.Select((f,i)=>(f,i)))
                        foreach (var pos in f.Slots.Where(p=>!p.Occupied && !f.EggAvailable))
                            Action($"depositparent/{i}/{pos.Index}","Deposit at "+f.Name,$"Parent slot {pos.Index+1}");
                if (rules.Grows && !parent.IsEgg) Edit("grow","Day Care experience to add","1");
                if (rules.Breeds && !parent.IsEgg) Row("n","parents","Choose second parent");
                if (!EggService.IsSupported(session)) { Info("Egg editing unavailable in this format."); break; }
                var egg = EggService.Get(session,box,slot);
                if (!egg.IsEgg)
                {
                    if (egg.MakeEggRefusal==null) Action("makeegg","Make an egg of this line");
                    else Info(egg.MakeEggRefusal);
                    break;
                }
                Action("hatch","Hatch this egg"); Edit("cycles",$"Hatch cycles ({egg.MinHatchCounter}-{egg.MaxHatchCounter})",egg.HatchCounter);
                Info("Estimated steps",egg.StepsLeft); Link("eggmoves","Egg moves"); break;
            case "parents":
                var first = Entity;
                for (int b=-1;b<Save.BoxCount;b++)
                for (int s=0;s<(b<0?Save.PartyCount:Save.BoxSlotCount);s++)
                {
                    if (b==box && s==slot) continue;
                    var other=session.GetEntity(b,s);
                    if (other.Species!=0 && ParkDaycareService.CanBreed(first,other))
                        Action($"breed/{b}/{s}",GameInfo.Strings.specieslist[other.Species],b<0?$"Party {s+1}":$"Box {b+1}, slot {s+1}");
                }
                break;
            case "breed":
                if (Index()==box && Index(2)==slot) throw new ArgumentException("Select two different parents.");
                var result = ParkDaycareService.MakeEgg(Entity,session.GetEntity(Index(),Index(2)),out var reason);
                if (result==null) throw new InvalidOperationException(reason);
                var dest=Empty();
                if (!session.ImportSlot(dest.Box,dest.Slot,result.Data,result.Format)) throw new InvalidOperationException("The egg cannot enter this save.");
                Finish($"Egg placed in box {dest.Box+1}, slot {dest.Slot+1}."); break;
            case "grow":
                var growth=ParkDaycareService.GrowSlot(session,box,slot,(uint)Number(1_000_000));
                Finish($"Gained {growth.ExpGained} EXP, now level {growth.LevelAfter}."); break;
            case "hatch": Finish(EggService.Hatch(session,box,slot)); break;
            case "makeegg": Finish(EggService.MakeEgg(session,box,slot)); break;
            case "cycles": Finish(EggService.SetHatchCounter(session,box,slot,Number(255))); break;
            case "eggmoves":
                Edit("seteggmoves","Set move IDs, comma separated",string.Join(',',EggService.Get(session,box,slot).Moves));
                foreach (var m in EggService.GetEggMoveChoices(session,box,slot)) Info($"{m.Move}. {m.Name}",m.Source);
                break;
            case "seteggmoves": Finish(EggService.SetEggMoves(session,box,slot,input.Split(',').Select(int.Parse).ToArray())); break;
            default: return false;
        }
        return true;
    }
}
