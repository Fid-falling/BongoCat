using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public sealed class MverBlindMetricResult
{
    public int Width { get; set; }
    public int Height { get; set; }
    public double Similarity { get; set; }
    public double WithinEight { get; set; }
    public double ForegroundSimilarity { get; set; }
    public double ForegroundWithinEight { get; set; }
    public long ForegroundSamples { get; set; }
    public string ForegroundMode { get; set; }
    public double ForegroundIoU { get; set; }
}

public static class MverBlindMetrics
{
    private sealed class Pixels : IDisposable
    {
        public Bitmap Bitmap;
        public byte[] Data;
        public int Width;
        public int Height;
        public int Stride;

        public int Offset(int x, int y) { return y * Stride + x * 4; }
        public void Dispose() { Bitmap.Dispose(); }
    }

    private static Pixels Load(string path)
    {
        using (var source = new Bitmap(path))
        {
            var bitmap = new Bitmap(source.Width, source.Height,
                PixelFormat.Format32bppArgb);
            using (Graphics graphics = Graphics.FromImage(bitmap))
            {
                graphics.CompositingMode = CompositingMode.SourceCopy;
                graphics.DrawImageUnscaled(source, 0, 0);
            }
            var rect = new Rectangle(0, 0, bitmap.Width, bitmap.Height);
            BitmapData locked = bitmap.LockBits(rect, ImageLockMode.ReadOnly,
                PixelFormat.Format32bppArgb);
            int stride = Math.Abs(locked.Stride);
            var data = new byte[stride * bitmap.Height];
            Marshal.Copy(locked.Scan0, data, 0, data.Length);
            bitmap.UnlockBits(locked);
            return new Pixels { Bitmap = bitmap, Data = data,
                Width = bitmap.Width, Height = bitmap.Height, Stride = stride };
        }
    }

    private static int Maximum(byte[] data, int offset)
    {
        return Math.Max(data[offset], Math.Max(data[offset + 1], data[offset + 2]));
    }

    public static MverBlindMetricResult Measure(string left, string right,
        string mask, int sampleStep, int alphaThreshold, int backgroundThreshold)
    {
        using (Pixels first = Load(left))
        using (Pixels second = Load(right))
        using (Pixels maskPixels = String.IsNullOrEmpty(mask) ? null : Load(mask))
        {
            if (first.Width != second.Width || first.Height != second.Height)
                throw new InvalidOperationException("Frame dimensions differ");
            int step = sampleStep > 0 ? sampleStep : Math.Max(1,
                (int)Math.Sqrt(first.Width * (double)first.Height / 250000.0));
            int alphaStep = Math.Max(step, 8);
            int alphaSamples = 0;
            int nonOpaque = 0;
            for (int y = 0; y < first.Height; y += alphaStep)
            for (int x = 0; x < first.Width; x += alphaStep)
            {
                int a = first.Offset(x, y);
                int b = second.Offset(x, y);
                if (first.Data[a + 3] < 255 || second.Data[b + 3] < 255)
                    nonOpaque++;
                alphaSamples++;
            }
            bool alphaVaries = nonOpaque >= Math.Max(2, alphaSamples / 100);
            double absolute = 0.0;
            double foregroundAbsolute = 0.0;
            long within = 0, samples = 0;
            long foregroundWithin = 0, foreground = 0;
            long foregroundUnion = 0, foregroundIntersection = 0;
            for (int y = 0; y < first.Height; y += step)
            for (int x = 0; x < first.Width; x += step)
            {
                int a = first.Offset(x, y);
                int b = second.Offset(x, y);
                int maximum = 0;
                for (int channel = 0; channel < 4; channel++)
                {
                    int delta = Math.Abs(first.Data[a + channel] - second.Data[b + channel]);
                    absolute += delta;
                    maximum = Math.Max(maximum, delta);
                }
                if (maximum <= 8) within++;
                bool firstVisible = alphaVaries
                    ? first.Data[a + 3] > alphaThreshold
                    : Maximum(first.Data, a) > backgroundThreshold;
                bool secondVisible = alphaVaries
                    ? second.Data[b + 3] > alphaThreshold
                    : Maximum(second.Data, b) > backgroundThreshold;
                if (firstVisible || secondVisible) foregroundUnion++;
                if (firstVisible && secondVisible) foregroundIntersection++;
                bool isForeground = firstVisible || secondVisible;
                if (maskPixels != null)
                {
                    int maskX = Math.Min(maskPixels.Width - 1,
                        (int)(x * (double)maskPixels.Width / first.Width));
                    int maskY = Math.Min(maskPixels.Height - 1,
                        (int)(y * (double)maskPixels.Height / first.Height));
                    int m = maskPixels.Offset(maskX, maskY);
                    isForeground = maskPixels.Data[m + 3] > alphaThreshold ||
                        Maximum(maskPixels.Data, m) > alphaThreshold;
                }
                if (isForeground)
                {
                    int foregroundMaximum = 0;
                    for (int channel = 0; channel < 4; channel++)
                    {
                        int delta = Math.Abs(first.Data[a + channel] -
                            second.Data[b + channel]);
                        foregroundAbsolute += delta;
                        foregroundMaximum = Math.Max(foregroundMaximum, delta);
                    }
                    if (foregroundMaximum <= 8) foregroundWithin++;
                    foreground++;
                }
                samples++;
            }
            if (foreground == 0)
                throw new InvalidOperationException("No foreground pixels found");
            return new MverBlindMetricResult {
                Width = first.Width, Height = first.Height,
                Similarity = 100.0 * (1.0 - absolute / (samples * 4.0 * 255.0)),
                WithinEight = 100.0 * within / samples,
                ForegroundSimilarity = 100.0 * (1.0 - foregroundAbsolute /
                    (foreground * 4.0 * 255.0)),
                ForegroundWithinEight = 100.0 * foregroundWithin / foreground,
                ForegroundSamples = foreground,
                ForegroundMode = maskPixels != null ? "mask" :
                    alphaVaries ? "alpha-union" : "black-background-union",
                ForegroundIoU = foregroundUnion != 0
                    ? 100.0 * foregroundIntersection / foregroundUnion : 100.0
            };
        }
    }
}
