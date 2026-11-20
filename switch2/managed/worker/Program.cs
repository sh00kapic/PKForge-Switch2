// SPDX-License-Identifier: GPL-3.0-or-later
using System.Globalization;
using PKForge.Domain;
using PKForge.Engine;
using PKHeX.Core;

try
{
    PortableCrypto.Install();
    using var session = new SaveEngine().OpenSession(File.ReadAllBytes("input.sav"), args.ElementAtOrDefault(4) ?? "main");
    if (session is not SaveEngineSession retail)
        throw new NotSupportedException("These tools require a supported retail save format.");
    retail.MakeActive();
    new Workbench(retail, args).Run();
    return 0;
}
catch (Exception error)
{
    Console.Error.WriteLine(error.GetBaseException().Message);
    return 1;
}

sealed partial class Workbench
{
    void EventTableStatus()
    {
        string[] catalogs = ["event1", "event2", "wc4", "pgf", "wc6", "wc6full", "wc7", "wc7full",
            "wb7full", "wc8", "wb8", "wa8", "wc9", "wa9"];
        if (catalogs.Any(c => !File.Exists("/mgdb-" + c + ".pkl")))
            Info("Event tables incomplete", "Event gifts may be flagged until the local MGDB is installed.");
    }

    readonly SaveEngineSession session;
    readonly string command, input, mode, filename;
    readonly int box, slot;
    readonly string[] parts;
    SaveFile Save => session.SaveFile;
    PKM Entity => session.GetEntity(box, slot);
    readonly LegalityService legality = new();
    readonly LegalizerService generator;
    sealed record Ownership(bool UseCurrentTrainerForGeneration) : IGenerationOwnershipSettings;

    public Workbench(SaveEngineSession session, string[] args)
    {
        this.session = session;
        mode = args[0];
        filename=args.ElementAtOrDefault(4) ?? "main";
        generator=new LegalizerService(new Ownership(args.ElementAtOrDefault(3)!="other"));
        var path = args[1].Split(':', 2);
        var coordinate = path[0].Split(',');
        box = int.Parse(coordinate[0]); slot = int.Parse(coordinate[1]);
        parts = path[1].Split('/');
        command = parts[0]; input = args.ElementAtOrDefault(2) ?? "";
    }
    static string Clean(object? value) => (value?.ToString() ?? "").Replace('\t',' ').Replace('\r',' ').Replace('\n',' ');
    void Row(string kind, string key, string label, object? value = null, int? destBox = null, int? destSlot = null)
    {
        Console.WriteLine($"{kind}\t{destBox ?? box},{destSlot ?? slot}:{key}\t{Clean(label)}\t{Clean(value)}");
    }
    void Info(string label, object? value = null) => Row("t", "", label, value);
    void Link(string key, string label, object? value = null) => Row("n", key, label, value);
    void Edit(string key, string label, object? value = null) => Row("i", key, label, value);
    void Action(string key, string label, object? value = null) => Row("w", key, label, value);
    int Number(int max = int.MaxValue, int min = 0)
    {
        if (!int.TryParse(input, out int value) || value < min || value > max)
            throw new ArgumentException($"Enter a number from {min} to {max}.");
        return value;
    }
    int Index(int position = 1) => int.Parse(parts[position], CultureInfo.InvariantCulture);
    void Finish(string message)
    {
        if (mode == "2" && command is not ("withdraw" or "depositparent")) throw new InvalidOperationException("Hardcore permits management only.");
        var bytes = session.Serialize();
        using var verify = new SaveEngine().OpenSession(bytes, filename);
        if (verify is not SaveEngineSession other || other.SaveFile.Version != Save.Version || !other.SaveFile.ChecksumsValid)
            throw new InvalidDataException("The edited copy failed its format or checksum check.");
        File.WriteAllBytes("changed.sav", bytes.ToArray());
        Info(message);
    }
    void Finish(GenerationOutcome outcome)
    {
        if (!outcome.Success) throw new InvalidOperationException(outcome.Message);
        Finish(outcome.Message);
    }
    (int Box,int Slot) Empty()
    {
        int next = Save.NextOpenBoxSlot();
        if (next < 0) throw new InvalidOperationException("All boxes are full.");
        return (next / Save.BoxSlotCount, next % Save.BoxSlotCount);
    }
    (int Species,int Form) SpeciesForm()
    {
        var fields = input.Split(',', StringSplitOptions.TrimEntries);
        if (fields.Length > 2) throw new ArgumentException("Use species ID or name, optionally followed by a comma and form number.");
        var names = GameInfo.Strings.specieslist;
        int species = int.TryParse(fields[0], out int id) ? id : Array.FindIndex(names, name => string.Equals(name, fields[0], StringComparison.OrdinalIgnoreCase));
        int form = fields.Length == 1 ? 0 : int.Parse(fields[1]);
        if (species <= 0 || species >= names.Length || form is < 0 or > 255) throw new ArgumentException("Unknown species or form.");
        return (species, form);
    }
    public void Run()
    {
        switch (command)
        {
            case "home":
                Link("trainer","Trainer profile and card"); Link("records","Trainer records");
                Link("party","Party Pokemon"); Link("boxes","Boxed Pokemon");
                Link("sweep","Full legality sweep","Party and all boxes");
                Row("q","encounters","Find encounters","Species name or ID, form");
                Edit("generate","Generate with AutoMod","Species name or ID, form");
                Edit("shiny","Generate shiny with AutoMod","Species name or ID, form");
                Edit("showdown","Generate Showdown set","Separate lines with |");
                Link("nursery","Day Care and breeding"); break;
            case "boxes":
                for (int b=0; b<Save.BoxCount; b++) Row("n","box","Box "+(b+1),(Save as IBoxDetailNameRead)?.GetBoxName(b),b,0);
                break;
            case "party": case "box":
                int bIndex = command == "party" ? -1 : box;
                int count = bIndex < 0 ? Save.PartyCount : Save.BoxSlotCount;
                for (int s=0; s<count; s++)
                {
                    var mon = session.GetEntity(bIndex,s);
                    if (mon.Species != 0) Row("n","slot",$"{s+1}. {GameInfo.Strings.specieslist[mon.Species]}",mon.Nickname,bIndex,s);
                }
                break;
            case "slot":
                Info(GameInfo.Strings.specieslist[Entity.Species], box < 0 ? $"Party {slot+1}" : $"Box {box+1}, slot {slot+1}");
                Link("report","Encounter legality"); Link("fixes","Legality presets"); Action("legalize","Repair with AutoMod");
                Link("rng","RNG tools"); Link("meta","Format-specific fields"); Link("tech","Technical records");
                Link("egg","Egg and breeding tools"); Action("mine","Use save trainer as owner"); break;
            case "report":
                EventTableStatus();
                if (!session.SupportsLegalityAnalysis) throw new NotSupportedException("No encounter tables for this format.");
                var report = legality.Analyze(session,box,slot);
                Info(report.Valid ? "Legal" : "Legality issues", LegalityAssistService.Shared.GetReport(session,box,slot).Encounter);
                foreach (var line in report.Lines) Info(line);
                Link("fixes","Legality presets"); Action("legalize","Repair with AutoMod");
                break;
            case "sweep":
                EventTableStatus();
                if (!session.SupportsLegalityAnalysis) throw new NotSupportedException("No encounter tables for this format.");
                var entries = new List<(int B,int S,bool Valid,string Reason)>();
                for (int b=-1; b<Save.BoxCount; b++)
                for (int s=0; s<(b<0 ? Save.PartyCount : Save.BoxSlotCount); s++)
                {
                    if (session.GetEntity(b,s).Species == 0) continue;
                    var r = legality.Analyze(session,b,s);
                    entries.Add((b,s,r.Valid,r.Valid ? "Legal" : r.Lines.FirstOrDefault() ?? "Invalid"));
                }
                Info($"{entries.Count} checked", $"{entries.Count(x=>!x.Valid)} with issues");
                foreach (var e in entries.OrderBy(x=>x.Valid))
                    Row("n","report",e.B<0 ? $"Party {e.S+1}" : $"Box {e.B+1}, slot {e.S+1}", e.Reason,e.B,e.S);
                break;
            case "fixes":
                Action("shinyon","Make shiny",mode=="1" ? "Allow raw change" : "Keep legal");
                Action("shinyoff","Remove shininess",mode=="1" ? "Allow raw change" : "Keep legal");
                if (Save.Generation>=6) Action("tradehandler","Set current trainer as handler");
                Action("heal","Restore HP, status and PP");
                foreach (var fix in Enum.GetValues<LegalityFix>()) Action("fix/"+fix,LegalityAssistService.TitleOf(fix));
                break;
            case "shinyon": case "shinyoff":
                Finish(BulkBoxActions.SetShiny(session,[(box,slot)],command=="shinyon",mode!="1").ToOutcome("Updated")); break;
            case "tradehandler": Finish(BulkBoxActions.SetCurrentTrainerAsHandler(session,[(box,slot)]).ToOutcome("Updated")); break;
            case "heal": Finish(BulkBoxActions.Heal(session,[(box,slot)]).ToOutcome("Healed")); break;
            case "fix":
                var preview = LegalityAssistService.Shared.Preview(session,box,slot,Enum.Parse<LegalityFix>(parts[1]));
                if (!preview.Available) throw new InvalidOperationException(preview.Unavailable);
                foreach (var c in preview.Changes) Info(c.Field,$"{c.Before} -> {c.After}");
                Finish(LegalityAssistService.Shared.Apply(session,preview)); break;
            case "legalize": Finish(generator.LegalizeSlot(session,box,slot)); break;
            case "encounters":
                var query = SpeciesForm(); var cards = session.GetEncounterCards(query.Species,query.Form);
                for (int i=0; i<cards.Count; i++)
                {
                    var c = cards[i];
                    Action($"place/{query.Species}/{query.Form}/{i}",c.Kind+" "+c.Location,$"{c.GameName}  Lv {c.LevelMin}-{c.LevelMax}  {c.Detail}");
                }
                if (cards.Count == 0) Info("No encounters in this game.");
                break;
            case "place":
                var dest = Empty(); Info("Destination",$"Box {dest.Box+1}, slot {dest.Slot+1}"); Finish(session.PlaceEncounter(Index(),Index(2),Index(3),dest.Box,dest.Slot)); break;
            case "generate": case "shiny": case "showdown":
                var target = Empty(); Info("Destination",$"Box {target.Box+1}, slot {target.Slot+1}");
                if (command == "showdown") Finish(generator.GenerateFromShowdown(session,target.Box,target.Slot,input.Replace('|','\n'),mode=="1"));
                else
                {
                    var spec = SpeciesForm();
                    Finish(generator.Generate(session,target.Box,target.Slot,new(spec.Species,null,command=="shiny",null,null,null,null,spec.Form,mode=="1")));
                }
                break;
            case "rng":
                var rng = session.GetRngInfo(box,slot);
                if (Save.Generation >= 3) Edit("pid","Personality ID (hex)",rng.Pid.ToString("X8"));
                if (rng.EncryptionConstant is uint ec) Edit("ec","Encryption constant (hex)",ec.ToString("X8"));
                if (rng.NatureRerollSupported) Link("natures","Reroll nature, keep shiny",GameInfo.Strings.natures[rng.Nature]);
                Info("IVs (HP/Atk/Def/SpA/SpD/Spe)",string.Join("/",rng.IVs)); Info("Shiny",rng.Shiny);
                Info("Ability",GameInfo.Strings.abilitylist[rng.Ability]); Info("Gender",rng.Gender); break;
            case "pid": case "ec":
                if (!MonFieldService.TryParseHex(input,out var raw)) throw new ArgumentException("Enter up to eight hexadecimal digits.");
                var change = command=="pid" ? MonFieldService.SetPid(session,box,slot,raw) : MonFieldService.SetEncryptionConstant(session,box,slot,raw);
                foreach (string line in change.Changes) Info(line);
                Finish("Personality fields reviewed."); break;
            case "natures":
                for(int i=0;i<25;i++) Action("reroll/"+i,GameInfo.Strings.natures[i]);
                break;
            case "reroll":
                if (!session.RerollNatureKeepShiny(box,slot,parts.Length>1 ? Index() : Number(24))) throw new InvalidOperationException("No matching reroll found.");
                Finish("Nature rerolled."); break;
            case "mine": Finish(session.MakeMine(box,slot)); break;
            default: if (!Trainer() && !Metadata() && !FormatFields() && !Breeding()) throw new ArgumentException("Unknown operation."); break;
        }
    }
}
