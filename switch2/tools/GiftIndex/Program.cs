using System.Text;
using PKForge.Domain;
using PKForge.Engine;
using PKHeX.Core;


if (args.Length != 2)
    throw new ArgumentException("Usage: GiftIndex <personal-card-folder> <new-output-folder>");
var source = Path.GetFullPath(args[0]);
var destination = Path.GetFullPath(args[1]);
if (!Directory.Exists(source) || Directory.Exists(destination) || File.Exists(destination))
    throw new IOException("Choose an existing source and a new output directory.");
var groups = new SortedDictionary<int, List<(string Name, byte[] Data)>>();
var extensions = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
    { ".pcd", ".wc4", ".pgt", ".pgf", ".wc6", ".wc6full", ".wc7", ".wc7full", ".wb7", ".wb7full", ".wc8", ".wb8", ".wa8", ".wc9", ".wa9" };
foreach (var path in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories).Order(StringComparer.Ordinal))
{
    var ext = Path.GetExtension(path);
    if (!extensions.Contains(ext)) continue;
    if (new FileInfo(path).Length is <= 0 or > 4096) throw new InvalidDataException(path);
    var bytes = File.ReadAllBytes(path);
    var card = MysteryGift.GetMysteryGift(bytes, ext.ToLowerInvariant()) ?? throw new InvalidDataException(path);
    if (!groups.TryGetValue(card.Generation, out var list)) groups[card.Generation] = list = [];
    list.Add((Path.ChangeExtension(Path.GetRelativePath(source, path), ext.ToLowerInvariant()).Replace(Path.DirectorySeparatorChar, '/'), bytes));
}
if (groups.Count == 0) throw new InvalidDataException("No supported card files were found.");
Directory.CreateDirectory(destination);
foreach (var (gen, cards) in groups)
{
    if (cards.Count > 10000) throw new InvalidDataException("Too many cards in this generation.");
    using var writer = new BinaryWriter(File.Create(Path.Combine(destination, $"gen{gen}.bin")));
    writer.Write(cards.Count);
    foreach (var (name, data) in cards)
    {
        var path = Encoding.UTF8.GetBytes(name);
        if (path.Length > 2048) throw new InvalidDataException("Card path is too long.");
        writer.Write((ushort)path.Length); writer.Write(path);
        writer.Write(data.Length); writer.Write(data);
    }
}

// Keep archive positions stable so a displayed language copy selects its original bytes.
foreach (var archive in Directory.GetFiles(destination, "gen*.bin").Order())
{
    using var input = new BinaryReader(File.OpenRead(archive));
    var count = input.ReadInt32();
    var seen = new HashSet<string>();
    var gifts = new List<EventGift>();
    for (int index = 0; index < count; index++)
    {
        var path = Encoding.UTF8.GetString(input.ReadBytes(input.ReadUInt16()));
        var bytes = input.ReadBytes(input.ReadInt32());
        var card = MysteryGift.GetMysteryGift(bytes, Path.GetExtension(path));
        if (card is DataMysteryGift data && seen.Add(Convert.ToBase64String(data.Write())) && EventDatabaseService.DescribeCard(card, index, path) is { } gift)
            gifts.Add(gift);
    }
    // No other card bytes are added to a personal archive.
    File.WriteAllBytes(Path.ChangeExtension(archive, ".extra"), new byte[4]);
    var entries = WonderCardAlbum.CreateEntries(gifts, id => GameInfo.Strings.specieslist[id], null, _ => false);
    using var output = new StreamWriter(Path.ChangeExtension(archive, ".tsv"), false, new UTF8Encoding(false));
    foreach (var e in entries)
    {
        var g = e.Gift;
        var d = g.Details!;
        object?[] fields = [g.Id, e.Title, e.SpeciesName, g.Species, g.Level, g.CardId, g.Shiny ? 1 : 0,
            e.LanguageTag, e.VariantKey, e.Date?.ToString("yyyy-MM-dd"), d.OriginalTrainer, d.TrainerId, e.Series,
            string.Join(", ", d.Moves), d.HeldItemName, d.BallName, d.MetLocation, e.Source?.Games,
            string.Join(", ", d.Items.Select(i => $"{i.Name} x{i.Quantity}")), g.Language, e.OriginalTitle, d.Ball];
        output.WriteLine(string.Join('\t', fields.Select(f => f?.ToString()?.Replace('\t',' ').Replace('\n',' ').Replace('\r',' '))));
    }
    Console.WriteLine($"{Path.GetFileName(archive)}: {entries.Count} cards");
}
