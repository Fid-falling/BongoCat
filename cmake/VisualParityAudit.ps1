param(
    [Parameter(Mandatory=$true)][string]$Reference,
    [Parameter(Mandatory=$true)][string]$Actual,
    [string]$DiffPath = "",
    [string]$JsonPath = "",
    [int]$X = 0,
    [int]$Y = 0,
    [int]$Width = 0,
    [int]$Height = 0
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
if (-not ("BongoCat.VisualParity" -as [type])) {
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace BongoCat {
public sealed class VisualParityResult {
    public int X { get; set; }
    public int Y { get; set; }
    public int Width { get; set; }
    public int Height { get; set; }
    public long Pixels { get; set; }
    public double MeanAbsoluteError { get; set; }
    public double RootMeanSquareError { get; set; }
    public double ExactPixelPercent { get; set; }
    public double WithinEightPercent { get; set; }
    public double SimilarityPercent { get; set; }
}

public static class VisualParity {
    private static int RowOffset(BitmapData data, int row, int height) {
        return data.Stride >= 0 ? row * data.Stride :
            (height - 1 - row) * -data.Stride;
    }

    public static VisualParityResult Compare(string referencePath,
        string actualPath, string diffPath, int x, int y, int width, int height) {
        using (var reference = new Bitmap(referencePath))
        using (var actual = new Bitmap(actualPath)) {
            if (reference.Size != actual.Size)
                throw new InvalidOperationException("Image dimensions differ");
            if (width <= 0) width = reference.Width - x;
            if (height <= 0) height = reference.Height - y;
            var area = new Rectangle(x, y, width, height);
            if (x < 0 || y < 0 || area.Right > reference.Width ||
                area.Bottom > reference.Height)
                throw new ArgumentOutOfRangeException("Comparison region");
            using (var first = reference.Clone(area, PixelFormat.Format32bppArgb))
            using (var second = actual.Clone(area, PixelFormat.Format32bppArgb))
            using (var diff = String.IsNullOrEmpty(diffPath) ? null :
                new Bitmap(width, height, PixelFormat.Format32bppArgb)) {
                var lockArea = new Rectangle(0, 0, width, height);
                var a = first.LockBits(lockArea, ImageLockMode.ReadOnly,
                    PixelFormat.Format32bppArgb);
                var b = second.LockBits(lockArea, ImageLockMode.ReadOnly,
                    PixelFormat.Format32bppArgb);
                BitmapData d = diff == null ? null : diff.LockBits(lockArea,
                    ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
                byte[] ap = new byte[Math.Abs(a.Stride) * height];
                byte[] bp = new byte[Math.Abs(b.Stride) * height];
                byte[] dp = d == null ? null : new byte[Math.Abs(d.Stride) * height];
                Marshal.Copy(a.Scan0, ap, 0, ap.Length);
                Marshal.Copy(b.Scan0, bp, 0, bp.Length);
                long absolute = 0, squared = 0, exact = 0, withinEight = 0;
                try {
                    for (int row = 0; row < height; ++row) {
                        int ar = RowOffset(a, row, height);
                        int br = RowOffset(b, row, height);
                        int dr = d == null ? 0 : RowOffset(d, row, height);
                        for (int column = 0; column < width; ++column) {
                            int ao = ar + column * 4, bo = br + column * 4;
                            int offset = dr + column * 4, maximum = 0;
                            for (int channel = 0; channel < 3; ++channel) {
                                int delta = Math.Abs(ap[ao + channel] -
                                    bp[bo + channel]);
                                absolute += delta; squared += delta * delta;
                                maximum = Math.Max(maximum, delta);
                                if (dp != null) dp[offset + channel] =
                                    (byte)Math.Min(255, delta * 4);
                            }
                            if (maximum == 0) exact++;
                            if (maximum <= 8) withinEight++;
                            if (dp != null) dp[offset + 3] = 255;
                        }
                    }
                    if (d != null) Marshal.Copy(dp, 0, d.Scan0, dp.Length);
                } finally {
                    first.UnlockBits(a); second.UnlockBits(b);
                    if (d != null) diff.UnlockBits(d);
                }
                if (diff != null) diff.Save(diffPath, ImageFormat.Png);
                double samples = (double)width * height * 3;
                double pixels = (double)width * height;
                double mae = absolute / samples / 255.0;
                return new VisualParityResult {
                    X=x, Y=y, Width=width, Height=height, Pixels=(long)pixels,
                    MeanAbsoluteError=mae,
                    RootMeanSquareError=Math.Sqrt(squared / samples) / 255.0,
                    ExactPixelPercent=exact * 100.0 / pixels,
                    WithinEightPercent=withinEight * 100.0 / pixels,
                    SimilarityPercent=(1.0 - mae) * 100.0
                };
            }
        }
    }
}
}
'@
}

$referencePath = [IO.Path]::GetFullPath($Reference)
$actualPath = [IO.Path]::GetFullPath($Actual)
if (-not [IO.File]::Exists($referencePath)) { throw "Missing reference: $referencePath" }
if (-not [IO.File]::Exists($actualPath)) { throw "Missing actual: $actualPath" }
$resolvedDiff = if ($DiffPath) { [IO.Path]::GetFullPath($DiffPath) } else { "" }
$result = [BongoCat.VisualParity]::Compare($referencePath, $actualPath,
    $resolvedDiff, $X, $Y, $Width, $Height)
if ($JsonPath) {
    $result | ConvertTo-Json | Set-Content -Encoding utf8 ([IO.Path]::GetFullPath($JsonPath))
}
$result
