// SPDX-License-Identifier: GPL-3.0-or-later
using PKForge.Domain;
using PKForge.Engine;
using PKHeX.Core;

sealed partial class Workbench
{
    int[] Numbers(int count, int maximum)
    {
        var values = input.Split(',',StringSplitOptions.TrimEntries).Select(int.Parse).ToArray();
        if (values.Length != count || values.Any(x => x < 0 || x > maximum))
            throw new ArgumentException($"Enter {count} numbers between 0 and {maximum}, separated by commas.");
        return values;
    }

    bool FormatFields()
    {
        switch (command)
        {
            case "moves":
                var details = session.GetMoveDetails(box,slot);
                for (int i=0; i<details.Moves.Count; i++)
                    Link($"pickmove/{i}",$"Move {i+1}",GameInfo.Strings.movelist[details.Moves[i].Move]);
                Edit("pp","Current PP for four moves",string.Join(',',details.Moves.Select(m=>m.PP)));
                Edit("ppups","PP Ups for four moves",string.Join(',',details.Moves.Select(m=>m.PPUps)));
                if (details.SupportsRelearn) Edit("relearn","Relearn move IDs",string.Join(',',details.RelearnMoves));
                if (session.GetMoveShop(box,slot).Supported) Link("shop","Move Shop purchases and mastery");
                break;
            case "pickmove":
                var moves = new MonInfoService().GetMoveChoices(session,box,slot);
                Action($"move/{Index()}/0","Clear move");
                bool hasLearnData=moves.Any(m=>m.Learn.IsLegal);
                foreach (var m in moves.Where(m=>mode=="1" || !hasLearnData || m.Learn.IsLegal).OrderByDescending(m=>m.Learn.IsLegal))
                    Action($"move/{Index()}/{m.Id}",GameInfo.Strings.movelist[m.Id],m.Learn.IsLegal?m.Learn.Label:"Outside learnset");
                break;
            case "move":
                int move=Index(2);
                EntityEdit change=Index() switch { 0=>new(Move1:move), 1=>new(Move2:move), 2=>new(Move3:move),
                    3=>new(Move4:move), _=>throw new ArgumentOutOfRangeException("move slot") };
                session.ApplyEdit(box,slot,change); Finish("Move updated."); break;
            case "pp": case "ppups": case "relearn":
                MoveDetailsEdit moveDetails=command switch { "pp"=>new(PP:Numbers(4,255)), "ppups"=>new(PPUps:Numbers(4,3)),
                    _=>new(RelearnMoves:Numbers(4,Save.MaxMoveID)) };
                session.ApplyMoveDetails(box,slot,moveDetails); Finish("Move data updated."); break;
            case "shop":
                foreach (var entry in session.GetMoveShop(box,slot).Entries)
                {
                    string name=GameInfo.Strings.movelist[entry.Move];
                    Action($"purchase/{entry.Index}/{(entry.Purchased?0:1)}",name,entry.Purchased?"Purchased":"Unpurchased");
                    Action($"master/{entry.Index}/{(entry.Mastered?0:1)}",name,entry.Mastered?"Mastered":"Unmastered");
                }
                break;
            case "purchase": case "master":
                session.ApplyMoveShopEdit(box,slot,command=="master"?new(Index(),Mastered:Index(2)!=0):new(Index(),Purchased:Index(2)!=0));
                Finish("Move Shop entry updated."); break;
            case "held":
                var names=((ISaveEngineSession)session).GetHeldItemNames();
                var holdable=new MonInfoService().GetHeldItems(session).ToHashSet();
                Action("item/0","Remove held item");
                for (int i=1; i<names.Count && i<=Save.MaxItemID; i++)
                    if (!string.IsNullOrEmpty(names[i]) && (mode=="1" || holdable.Contains(i)))
                        Action("item/"+i,names[i],holdable.Contains(i)?"Holdable":"Outside holdable list");
                break;
            case "item": session.ApplyEdit(box,slot,new(HeldItem:Index())); Finish("Held item updated."); break;
            case "cosmetic":
                var c=session.GetCosmetics(box,slot);
                for (int i=0;i<c.Markings.Count;i++) Edit("appearance/mark/"+i,c.Markings[i].Name,c.Markings[i].Value);
                if (c.ContestStats.Count!=0) Edit("appearance/contest","Cool, beauty, cute, smart, tough, sheen",string.Join(',',c.ContestStats));
                if (c.SupportsSize) { Edit("appearance/height","Height scalar",c.HeightScalar); Edit("appearance/weight","Weight scalar",c.WeightScalar); }
                if (c.SupportsScale) Edit("appearance/scale","Scale",c.Scale);
                if (c.SupportsAffection) { Edit("appearance/ot","OT affection",c.OriginalTrainerAffection); Edit("appearance/ht","Handler affection",c.HandlingTrainerAffection); }
                if (c.SupportsFullnessEnjoyment) { Edit("appearance/full","Fullness",c.Fullness); Edit("appearance/joy","Enjoyment",c.Enjoyment); }
                if (c.SupportsFavorite) Action("appearance/favorite","Favorite",c.IsFavorite);
                if (c.SupportsDynamax) { Edit("appearance/dmax","Dynamax level",c.DynamaxLevel); Action("appearance/gmax","Gigantamax factor",c.CanGigantamax); }
                if (c.SupportsAlpha) Action("appearance/alpha","Alpha",c.IsAlpha);
                if (c.SupportsSociability) Edit("appearance/social","Sociability",c.Sociability);
                break;
            case "appearance":
                var before=session.GetCosmetics(box,slot);
                CosmeticEdit cosmetic=parts[1] switch {
                    "mark"=>new(Markings:before.Markings.Select((m,i)=>i==Index(2)?Number(m.MaxValue):m.Value).ToArray()),
                    "contest"=>new(ContestStats:Numbers(6,255)), "height"=>new(HeightScalar:Number(255)),
                    "weight"=>new(WeightScalar:Number(255)), "scale"=>new(Scale:Number(255)),
                    "ot"=>new(OriginalTrainerAffection:Number(255)), "ht"=>new(HandlingTrainerAffection:Number(255)),
                    "full"=>new(Fullness:Number(255)), "joy"=>new(Enjoyment:Number(255)), "favorite"=>new(IsFavorite:!before.IsFavorite),
                    "dmax"=>new(DynamaxLevel:Number(10)), "gmax"=>new(CanGigantamax:!before.CanGigantamax),
                    "alpha"=>new(IsAlpha:!before.IsAlpha), "social"=>new(Sociability:uint.Parse(input)),
                    _=>throw new ArgumentException("Unknown appearance field.") };
                session.ApplyCosmeticEdit(box,slot,cosmetic); Finish("Appearance updated."); break;
            case "origin":
                var met=session.GetMetInfo(box,slot);
                Info("Origin game",met.VersionName); Info("Language",met.LanguageName);
                Info("Met location",met.MetLocationName); Info("Egg location",met.EggLocationName);
                if (met.SupportsMetDate) Edit("origindate/met","Met date (YYYY-MM-DD)",met.MetDate);
                if (met.SupportsEggDate) Edit("origindate/egg","Egg date (YYYY-MM-DD)",met.EggDate);
                if (met.SupportsWasEgg) Action("wasegg","Hatched from an egg",met.WasEgg);
                break;
            case "origindate":
                DateOnly.ParseExact(input,"yyyy-MM-dd",System.Globalization.CultureInfo.InvariantCulture);
                session.ApplyMetEdit(box,slot,parts[1]=="egg"?new(EggDate:input):new(MetDate:input)); Finish("Date updated."); break;
            case "wasegg":
                session.ApplyMetEdit(box,slot,new(WasEgg:!session.GetMetInfo(box,slot).WasEgg)); Finish("Egg origin updated."); break;
            case "effort":
                var potential=session.GetPotential(box,slot);
                if (potential.SupportsGanbaru) Edit("ganbaru","Effort levels (HP/Atk/Def/SpA/SpD/Spe)",string.Join(',',potential.Ganbaru));
                if (potential.SupportsAwakening) Edit("awakening","Awakening values (HP/Atk/Def/SpA/SpD/Spe)",string.Join(',',potential.Awakening));
                break;
            case "ganbaru": case "awakening":
                session.ApplyPotentialEdit(box,slot,command=="ganbaru"?new(Ganbaru:Numbers(6,10)):new(Awakening:Numbers(6,200)));
                Finish("Training values updated."); break;
            default:return false;
        }
        return true;
    }
}
