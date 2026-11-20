// SPDX-License-Identifier: GPL-3.0-or-later
using PKForge.Domain;
using PKForge.Engine;
using PKHeX.Core;
using System.Globalization;
using System.Text;

sealed partial class Workbench
{
    bool Trainer()
    {
        switch (command)
        {
            case "trainer":
                var t = session.GetTrainer();
                Edit("identity/name","Trainer name",t.Name); Edit("identity/tid","Trainer ID",t.TID);
                if (Save.Generation >= 3) Edit("identity/sid","Secret ID",t.SID);
                if (Save.Generation>=3 || Save.Version==GameVersion.C) Edit("identity/gender","Trainer gender (0/1)",t.Gender); Edit("identity/money","Money",t.Money);
                string token = Convert.ToBase64String(Encoding.UTF8.GetBytes(string.Join('\t',t.Name,t.TID,t.SID,t.Gender)));
                Row("p","profile","Save a named trainer profile",token);
                Row("l","profile","Load a trainer profile"); Row("o","ownership","Use save trainer for generation");
                Link("card","Trainer card details"); Link("stats","Play time and currencies"); Link("records","Trainer records"); break;
            case "profile": case "owner":
                var values = Encoding.UTF8.GetString(Convert.FromBase64String(input)).Split('\t');
                var profile = new TrainerProfile("","",values[0],int.Parse(values[1]),int.Parse(values[2]),int.Parse(values[3]));
                if (command == "owner") Finish(session.MakeMine(box,slot,profile));
                else
                {
                    session.SetTrainer(session.GetTrainer() with { Name=profile.OriginalTrainer,TID=profile.TID,SID=profile.SID,Gender=profile.Gender });
                    Finish("Trainer identity applied.");
                }
                break;
            case "identity":
                var old = session.GetTrainer();
                var changed = parts[1] switch {
                    "name" when input.Length>0 => old with { Name=input }, "tid"=>old with { TID=Number(65535) },
                    "sid"=>old with { SID=Number(65535) }, "gender"=>old with { Gender=Number(1) },
                    "money"=>old with { Money=(uint)Number((int)Math.Min(Save.MaxMoney,int.MaxValue)) },
                    _=>throw new ArgumentException("Invalid trainer field.") };
                session.SetTrainer(changed); Finish("Trainer identity updated."); break;
            case "records":
                var table = session.GetTrainerRecords();
                if (!table.Supported) { Info("This game has no supported record table."); break; }
                var names = Save switch { SAV8BS=>Record8b.RecordList_8b, SAV8SWSH=>RecordLists.RecordList_8, SAV7=>RecordLists.RecordList_7,
                    SAV6=>RecordLists.RecordList_6, SAV5=>RecordLists.RecordList_5, _=>null };
                foreach (var r in table.Records)
                    Edit("record/"+r.Index,names?.GetValueOrDefault(r.Index) ?? $"Record {r.Index+1}",r.Value);
                break;
            case "record":
                var records = (ITrainerStatRecord)Save;
                var record = Index(); if ((uint)record >= records.RecordCount) throw new ArgumentOutOfRangeException("record");
                var amount = Number(records.GetRecordMax(record)); records.SetRecord(record,amount);
                if (records.GetRecord(record)!=amount) throw new InvalidDataException("The record did not retain this value.");
                Finish($"Record {record+1} set to {amount}."); break;
            case "stats":
                var stats = session.GetTrainerStats();
                if (stats.SupportsPlayTime) { Edit("stat/hours","Play time hours",stats.PlayTimeHours); Edit("stat/minutes","Play time minutes",stats.PlayTimeMinutes); }
                if (stats.SupportsBP) Edit("stat/bp","Battle Points",stats.BP);
                if (stats.SupportsCoins) Edit("stat/coins","Coins",stats.Coins);
                if (stats.SupportsWatts) Edit("stat/watts","Watts",stats.Watts);
                if (stats.SupportsFestaCoins) Edit("stat/festa","Festival Coins",stats.FestaCoins);
                if (stats.SupportsLeaguePoints) Edit("stat/lp","League Points",stats.LeaguePoints);
                if (stats.SupportsBlueberryPoints) Edit("stat/blue","Blueberry Points",stats.BlueberryPoints);
                break;
            case "stat":
                var s = session.GetTrainerStats();
                TrainerStatsEdit stat = parts[1] switch {
                    "hours"=>new(PlayTimeHours:Number(s.PlayTimeHoursMax)), "minutes"=>new(PlayTimeMinutes:Number(59)),
                    "bp"=>new(BP:Number(s.BPMax)), "coins"=>new(Coins:Number(s.CoinsMax)), "watts"=>new(Watts:Number(s.WattsMax)),
                    "festa"=>new(FestaCoins:Number(s.FestaCoinsMax)), "lp"=>new(LeaguePoints:Number(s.LeaguePointsMax)),
                    "blue"=>new(BlueberryPoints:Number(s.BlueberryPointsMax)), _=>throw new ArgumentException("Unknown currency.") };
                session.SetTrainerStats(stat); Finish("Trainer statistic updated."); break;
            case "card":
                var c = TrainerCardService.Get(session);
                if (c.SupportsRivalName) Edit("cardedit/rival","Rival name",c.RivalName);
                foreach (var b in c.Badges) Action($"cardedit/badge/{b.Index}",b.Region+" / "+b.Name,b.Obtained ? "Obtained":"Missing");
                if (c.Geo.Supported) { Edit("cardedit/country","Country",c.Geo.Country); Edit("cardedit/region","Subregion",c.Geo.Region); }
                if (c.Geo.SupportsConsoleRegion) Edit("cardedit/console","Console region",c.Geo.ConsoleRegion);
                foreach (var (key,label,date) in new[]{("start","Adventure start",c.StartDate),("hof","Hall of Fame date",c.HallOfFameDate),("last","Last saved",c.LastSaved)})
                    if (date.Supported) Edit("cardedit/"+key,label,date.Value?.ToString(date.HasTime?"yyyy-MM-dd HH:mm:ss":"yyyy-MM-dd"));
                foreach (var f in c.FlyDestinations) Action($"cardedit/fly/{f.Index}",f.Name,f.Unlocked?"Unlocked":"Locked");
                break;
            case "cardedit":
                var card = TrainerCardService.Get(session);
                DateTime Date() => DateTime.ParseExact(input,["yyyy-MM-dd","yyyy-MM-dd HH:mm:ss"],CultureInfo.InvariantCulture,DateTimeStyles.None);
                TrainerCardEdit edit = parts[1] switch {
                    "rival"=>new(RivalName:input), "country"=>new(Country:Number(card.Geo.Max)), "region"=>new(Region:Number(card.Geo.Max)),
                    "console"=>new(ConsoleRegion:Number(6)), "start"=>new(StartDate:Date()), "hof"=>new(HallOfFameDate:Date()),
                    "last"=>new(LastSaved:Date()), "badge"=>new(Badges:card.Badges.Select(b=>b.Obtained ^ (b.Index==Index(2))).ToArray()),
                    "fly"=>new(FlyDestinations:card.FlyDestinations.Select(f=>f.Unlocked ^ (f.Index==Index(2))).ToArray()),
                    _=>throw new ArgumentException("Unknown trainer card field.") };
                TrainerCardService.Apply(session,edit); Finish("Trainer card updated."); break;
            default: return false;
        }
        return true;
    }
}
