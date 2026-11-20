using PKForge.Domain;
using Xunit;

namespace PKForge.Domain.Tests;

public sealed class LumiSpriteTests
{
    private static readonly (int Species, int Form, bool Female)[] Forms =
        [(3, 3, true), (6, 4, false), (9, 3, false), (25, 17, true), (94, 3, false), (95, 1, false), (150, 3, false)];

    [Fact]
    public void LumiFormsTryTheirOwnIconFirstShinyAndFemaleIncluded()
    {
        var shinyFemale = SpriteCatalog.BundledCandidates(new SpriteLook(3, 3, true, new SpriteTraits { Female = true }));
        Assert.Equal("sprites/lumi/b_3_L3fs.png", shinyFemale[0].Path);
        Assert.Equal("sprites/lumi/b_3_L3f.png", shinyFemale[1].Path);
        Assert.Equal("sprites/lumi/b_94_L3.png", SpriteCatalog.BundledCandidates(new SpriteLook(94, 3, false))[0].Path);
        // A retail form of the same species never looks for a Lumi icon.
        Assert.DoesNotContain(SpriteCatalog.BundledCandidates(new SpriteLook(94, 1, false)), c => c.Path.Contains("/lumi/", StringComparison.Ordinal));
    }

}
