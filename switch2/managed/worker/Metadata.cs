// SPDX-License-Identifier: GPL-3.0-or-later
using PKForge.Domain;
using PKForge.Engine;
using PKHeX.Core;

sealed partial class Workbench
{
    bool Metadata()
    {
        switch (command)
        {
            case "meta":
                var pk = Entity;
                if (Save.Generation>=3) Link("abilities","Ability");
                Info(pk.GetType().Name, $"{pk.Version} / {pk.Context}");
                if (MonFieldService.GetForm(session,box,slot) != null) Link("forms","Species form");
                if (MonFieldService.GetFormArgument(session,box,slot) is {} arg) Edit("arg",arg.Label,arg.Value);
                if (MonFieldService.GetStatNature(session,box,slot) is int nature) Edit("mint","Stat nature (0-24)",nature);
                var shine = MonFieldService.GetShiny(session,box,slot);
                if (shine.SupportsKind) Link("sparkle","Shiny effect",shine.Kind);
                Link("origin","Origin and egg dates");
                Link("moves","Moves, PP and relearn data");
                if (Save.HeldItems.Length!=0) Link("held","Held item");
                Link("cosmetic","Appearance and markings");
                var potential=session.GetPotential(box,slot);
                if (potential.SupportsAwakening || potential.SupportsGanbaru) Link("effort","Training values");
                var trainers = MonFieldService.GetTrainers(session,box,slot);
                if (trainers.OtGender.HasValue) Edit("handler/otgender","Original trainer gender",trainers.OtGender);
                Edit("handler/otfriend","Original trainer friendship",trainers.OtFriendship);
                if (trainers.HasHandler)
                {
                    Edit("handler/name","Handler name",trainers.HandlerName); Edit("handler/gender","Handler gender",trainers.HandlerGender);
                    Edit("handler/friend","Handler friendship",trainers.HandlerFriendship); Edit("handler/current","Current handler",trainers.CurrentHandler);
                    if (trainers.HandlerLanguage is int lang) Edit("handler/lang","Handler language",lang);
                }
                Row("l","owner","Assign named trainer profile");
                foreach (bool handler in new[]{false,true})
                    if (MonFieldService.GetMemory(session,box,slot,handler) != null) Link(handler?"memory/1":"memory/0",handler?"Handler memory":"Original trainer memory");
                break;
            case "abilities":
                var original=Entity;
                var own=session.GetAbilityChoices(original.Species,original.Form);
                IEnumerable<int> choices=mode=="1" && session.StoresAnyAbility ? Enumerable.Range(0,Save.MaxAbilityID+1) : own;
                foreach (int id in choices.Distinct()) Action("ability/"+id,GameInfo.Strings.abilitylist[id],original.Ability==id?"Current":"");
                break;
            case "ability":
                var mon=Entity;
                var eligible=session.GetAbilityChoices(mon.Species,mon.Form).ToList();
                int choice=Index();
                if (!eligible.Contains(choice) && !(mode=="1" && session.StoresAnyAbility && choice>=0 && choice<=Save.MaxAbilityID))
                    throw new ArgumentException("This ability cannot be stored.");
                session.ApplyEdit(box,slot,new(Ability:choice)); Finish("Ability updated."); break;
            case "sparkle":
                foreach (var k in Enum.GetValues<ShinyKind>()) Action("setshine/"+(int)k,k.ToString());
                break;
            case "setshine":
                MonFieldService.SetShinyKind(session,box,slot,(ShinyKind)Index()); Finish("Shiny effect updated."); break;
            case "forms":
                foreach (var f in MonFieldService.GetForm(session,box,slot)!.Choices.Where(f=>mode=="1" || (f.InGame && !f.BattleOnly)))
                    Action("form/"+f.Form, f.Name, f.BattleOnly?"Battle only":f.InGame?"In game":"Outside this game");
                break;
            case "form":
                var form = MonFieldService.SetForm(session,box,slot,Index());
                foreach (string msg in form.Notes) Info(msg);
                if (form.Warning != null) Info(form.Warning);
                Finish("Form reviewed."); break;
            case "arg":
                MonFieldService.SetFormArgument(session,box,slot,uint.Parse(input)); Finish("Form argument updated."); break;
            case "mint":
                MonFieldService.SetStatNature(session,box,slot,Number(24)); Finish("Stat nature updated."); break;
            case "handler":
                TrainerFieldsEdit edit = parts[1] switch {
                    "otgender"=>new(OtGender:Number(1)), "otfriend"=>new(OtFriendship:Number(255)), "name"=>new(HandlerName:input),
                    "gender"=>new(HandlerGender:Number(1)), "friend"=>new(HandlerFriendship:Number(255)), "current"=>new(CurrentHandler:Number(1)),
                    "lang"=>new(HandlerLanguage:Number(10)), _=>throw new ArgumentException("Unknown handler field.") };
                MonFieldService.ApplyTrainerEdit(session,box,slot,edit); Finish("Trainer fields updated."); break;
            case "memory":
                bool ht = Index()!=0;
                var mem = MonFieldService.GetMemory(session,box,slot,ht)!;
                Info(mem.Text);
                if (!mem.Editable) { Info(mem.Reason ?? "Read only."); break; }
                var options=MonFieldService.GetMemoryOptions(session,box,slot,ht,mem.Memory);
                foreach (var m in options.Memories.Where(x=>mode=="1" || x.Legal || x.Id==0))
                {
                    if (m.Id==0) Action($"savememory/{Index()}/0/0/0/0",m.Name);
                    else Link($"memarg/{Index()}/{m.Id}",m.Name,m.Id==mem.Memory?"Current":"");
                }
                break;
            case "memarg": case "memint": case "memfeel":
                var opt=MonFieldService.GetMemoryOptions(session,box,slot,Index()!=0,Index(2));
                string path=$"{Index()}/{Index(2)}";
                if (command=="memarg" && opt.Arguments.Count!=0)
                    foreach (var argChoice in opt.Arguments) Link($"memint/{path}/{argChoice.Id}",argChoice.Name);
                else if (command!="memfeel")
                {
                    int argId=command=="memarg"?0:Index(3);
                    foreach (var intensity in opt.Intensities.Where(x=>mode=="1" || x.Legal))
                        Link($"memfeel/{path}/{argId}/{intensity.Id}",intensity.Name);
                }
                else foreach (var feeling in opt.Feelings.Where(x=>mode=="1" || x.Legal))
                    Action($"savememory/{path}/{Index(3)}/{Index(4)}/{feeling.Id}",feeling.Name);
                break;
            case "savememory":
                MonFieldService.SetMemory(session,box,slot,new(Index()!=0,Index(2),Index(4),Index(5),Index(3)));
                Finish("Memory updated."); break;
            case "tech":
                var tech = MonFieldService.GetTechRecords(session,box,slot);
                if (tech == null) { Info("This format has no technical records."); break; }
                Action("techall","Set all legal records"); Action("techclear","Clear all records");
                foreach (var r in tech.Where(r=>mode=="1" || r.Permitted || r.Learned)) Action($"techset/{r.Index}/{(r.Learned?0:1)}",r.Label+" "+r.Name,(r.Learned?"Learned":"Clear")+(r.Permitted?"":" / incompatible"));
                break;
            case "techset": MonFieldService.SetTechRecord(session,box,slot,Index(),Index(2)!=0); Finish("Technical record updated."); break;
            case "techall": Finish($"{MonFieldService.SetAllLegalTechRecords(session,box,slot)} records set."); break;
            case "techclear": Finish($"{MonFieldService.ClearTechRecords(session,box,slot)} records cleared."); break;
            default: return false;
        }
        return true;
    }
}
