using System; using System.Collections.Generic; using System.Drawing;
using System.Drawing.Drawing2D; using System.Drawing.Imaging;
using System.Globalization; using System.IO; using System.Runtime.InteropServices;

public static class CubismViewerBlindTest
{
    const int Canvas = 800, Margin = 40;

    sealed class Frame { public string Name, Viewer, Native;
        public Bitmap ViewerImage, NativeImage; }

    sealed class Pixels : IDisposable
    {
        public Bitmap Image;
        public byte[] Data;
        public int Stride;
        public Pixels(Bitmap source)
        {
            Image = new Bitmap(source.Width, source.Height,
                PixelFormat.Format32bppArgb);
            using (Graphics g = Graphics.FromImage(Image))
                g.DrawImageUnscaled(source, 0, 0);
            BitmapData bits = Image.LockBits(new Rectangle(0, 0,
                Image.Width, Image.Height), ImageLockMode.ReadOnly,
                PixelFormat.Format32bppArgb);
            Stride = Math.Abs(bits.Stride);
            Data = new byte[Stride * Image.Height];
            Marshal.Copy(bits.Scan0, Data, 0, Data.Length);
            Image.UnlockBits(bits);
        }
        public int Offset(int x, int y) { return y * Stride + x * 4; }
        public void Dispose() { Image.Dispose(); }
    }

    struct Region { public string Name; public Rectangle Area;
        public Region(string name, Rectangle area) { Name = name; Area = area; }
    }

    static bool Visible(Color pixel, Color background, bool alpha)
    {
        if (alpha) return pixel.A > 8;
        int delta = Math.Max(Math.Abs(pixel.R - background.R),
            Math.Max(Math.Abs(pixel.G - background.G),
                Math.Abs(pixel.B - background.B)));
        return delta > 18;
    }

    static Rectangle Bounds(string path)
    {
        using (Bitmap image = new Bitmap(path))
        {
            Color background = image.GetPixel(image.Width / 2,
                Math.Min(image.Height - 1, Math.Max(1, image.Height / 20)));
            int alphaSamples = 0, nonOpaque = 0;
            for (int y = 0; y < image.Height; y += Math.Max(1, image.Height / 32))
            for (int x = 0; x < image.Width; x += Math.Max(1, image.Width / 32))
            {
                if (image.GetPixel(x, y).A < 250) nonOpaque++;
                alphaSamples++;
            }
            bool alpha = nonOpaque >= Math.Max(2, alphaSamples / 10);
            int size = image.Width * image.Height;
            bool[] foreground = new bool[size];
            for (int y = 0; y < image.Height; ++y)
            for (int x = 0; x < image.Width; ++x)
                foreground[y * image.Width + x] =
                    Visible(image.GetPixel(x, y), background, alpha);
            int[] queue = new int[size];
            int bestCount = 0, bestLeft = 0, bestTop = 0, bestRight = -1,
                bestBottom = -1;
            for (int start = 0; start < size; ++start)
            {
                if (!foreground[start]) continue;
                int head = 0, tail = 0, count = 0;
                int left = image.Width, top = image.Height, right = -1, bottom = -1;
                queue[tail++] = start; foreground[start] = false;
                while (head < tail)
                {
                    int current = queue[head++], x = current % image.Width;
                    int y = current / image.Width; count++;
                    left = Math.Min(left, x); top = Math.Min(top, y);
                    right = Math.Max(right, x); bottom = Math.Max(bottom, y);
                    int[] next = { current - 1, current + 1,
                        current - image.Width, current + image.Width };
                    for (int i = 0; i < next.Length; ++i)
                    {
                        int value = next[i];
                        if (value < 0 || value >= size || !foreground[value] ||
                            (i == 0 && x == 0) ||
                            (i == 1 && x == image.Width - 1)) continue;
                        foreground[value] = false; queue[tail++] = value;
                    }
                }
                if (count <= bestCount) continue;
                bestCount = count; bestLeft = left; bestTop = top;
                bestRight = right; bestBottom = bottom;
            }
            if (bestRight < bestLeft || bestBottom < bestTop)
                throw new InvalidOperationException("No model foreground in " + path);
            return Rectangle.FromLTRB(bestLeft, bestTop,
                bestRight + 1, bestBottom + 1);
        }
    }

    static Bitmap Normalize(string path, Rectangle anchor)
    {
        using (Bitmap source = new Bitmap(path))
        {
            double scale = Math.Min((Canvas - 2.0 * Margin) / anchor.Width,
                (Canvas - 2.0 * Margin) / anchor.Height);
            float left = (float)((Canvas - anchor.Width * scale) / 2.0 -
                anchor.Left * scale);
            float top = (float)((Canvas - anchor.Height * scale) / 2.0 -
                anchor.Top * scale);
            int padding = Math.Max(4, Math.Max(anchor.Width, anchor.Height) / 40);
            Rectangle crop = Rectangle.Intersect(new Rectangle(0, 0,
                source.Width, source.Height), new Rectangle(anchor.Left - padding,
                anchor.Top - padding, anchor.Width + padding * 2,
                anchor.Height + padding * 2));
            RectangleF destination = new RectangleF(
                left + crop.Left * (float)scale,
                top + crop.Top * (float)scale,
                crop.Width * (float)scale, crop.Height * (float)scale);
            Bitmap output = new Bitmap(Canvas, Canvas, PixelFormat.Format32bppArgb);
            using (Graphics g = Graphics.FromImage(output))
            {
                g.Clear(Color.White);
                g.CompositingMode = CompositingMode.SourceOver;
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                g.DrawImage(source, destination, crop, GraphicsUnit.Pixel);
            }
            return output;
        }
    }

    static bool Foreground(Pixels pixels, int offset)
    {
        return Math.Min(pixels.Data[offset], Math.Min(pixels.Data[offset + 1],
            pixels.Data[offset + 2])) < 237;
    }

    static void Measure(TextWriter writer, Frame frame, Region region,
        Pixels viewer, Pixels native, Pixels viewerBase, Pixels nativeBase)
    {
        long count = 0, within = 0, intersection = 0, union = 0;
        double absolute = 0, viewerChange = 0, nativeChange = 0;
        for (int y = region.Area.Top; y < region.Area.Bottom; y += 2)
        for (int x = region.Area.Left; x < region.Area.Right; x += 2)
        {
            int a = viewer.Offset(x, y), b = native.Offset(x, y);
            int av = viewerBase.Offset(x, y), bn = nativeBase.Offset(x, y);
            bool first = Foreground(viewer, a), second = Foreground(native, b);
            if (first || second) union++;
            if (first && second) intersection++;
            if (!first && !second) continue;
            int maximum = 0;
            for (int channel = 0; channel < 3; ++channel)
            {
                int delta = Math.Abs(viewer.Data[a + channel] -
                    native.Data[b + channel]);
                maximum = Math.Max(maximum, delta);
                absolute += delta;
                viewerChange += Math.Abs(viewer.Data[a + channel] -
                    viewerBase.Data[av + channel]);
                nativeChange += Math.Abs(native.Data[b + channel] -
                    nativeBase.Data[bn + channel]);
            }
            if (maximum <= 8) within++;
            count++;
        }
        double similarity = count == 0 ? 100 :
            100 * (1 - absolute / (count * 3 * 255.0));
        double near = count == 0 ? 100 : 100.0 * within / count;
        double iou = union == 0 ? 100 : 100.0 * intersection / union;
        double vc = count == 0 ? 0 : viewerChange / (count * 3 * 255.0);
        double nc = count == 0 ? 0 : nativeChange / (count * 3 * 255.0);
        double ratio = vc < 0.000001 ? (nc < 0.000001 ? 1 : 999) : nc / vc;
        writer.WriteLine(String.Join(",", frame.Name, region.Name,
            similarity.ToString("F4", CultureInfo.InvariantCulture),
            near.ToString("F4", CultureInfo.InvariantCulture),
            iou.ToString("F4", CultureInfo.InvariantCulture),
            vc.ToString("F6", CultureInfo.InvariantCulture),
            nc.ToString("F6", CultureInfo.InvariantCulture),
            ratio.ToString("F4", CultureInfo.InvariantCulture), count));
    }

    static void Ballot(Frame frame, string path, bool viewerFirst)
    {
        using (Bitmap ballot = new Bitmap(Canvas * 2 + 30, Canvas + 60))
        using (Graphics g = Graphics.FromImage(ballot))
        using (Font font = new Font("Segoe UI", 20, FontStyle.Bold))
        {
            g.Clear(Color.FromArgb(238, 238, 238));
            Bitmap first = viewerFirst ? frame.ViewerImage : frame.NativeImage;
            Bitmap second = viewerFirst ? frame.NativeImage : frame.ViewerImage;
            g.DrawImageUnscaled(first, 0, 60);
            g.DrawImageUnscaled(second, Canvas + 30, 60);
            g.DrawString("A", font, Brushes.Black, Canvas / 2 - 10, 15);
            g.DrawString("B", font, Brushes.Black, Canvas + 30 + Canvas / 2 - 10, 15);
            ballot.Save(path, ImageFormat.Png);
        }
    }

    static string FindNative(string directory, string name)
    {
        foreach (string extension in new[] { ".bmp", ".png" })
        {
            string path = Path.Combine(directory, name + extension);
            if (File.Exists(path)) return path;
        }
        return null;
    }

    public static string Run(string viewerDirectory, string nativeDirectory,
        string outputDirectory, int seed)
    {
        Directory.CreateDirectory(outputDirectory);
        string normalized = Path.Combine(outputDirectory, "normalized");
        string ballots = Path.Combine(outputDirectory, "ballots");
        Directory.CreateDirectory(normalized); Directory.CreateDirectory(ballots);
        var frames = new List<Frame>();
        var viewerFiles = new List<string>();
        viewerFiles.AddRange(Directory.GetFiles(viewerDirectory, "*.png"));
        viewerFiles.AddRange(Directory.GetFiles(viewerDirectory, "*.bmp"));
        foreach (string viewer in viewerFiles)
        {
            string name = Path.GetFileNameWithoutExtension(viewer);
            string native = FindNative(nativeDirectory, name);
            if (native != null) frames.Add(new Frame { Name = name,
                Viewer = viewer, Native = native });
        }
        frames.Sort((a, b) => String.CompareOrdinal(a.Name, b.Name));
        if (frames.Count == 0) throw new InvalidOperationException("No frame pairs");
        Frame baseline = frames.Find(f => f.Name == "track-000") ?? frames[0];
        string cleanViewer = Path.Combine(viewerDirectory, "idle.png");
        Rectangle viewerBounds = Bounds(File.Exists(cleanViewer)
            ? cleanViewer : baseline.Viewer);
        Rectangle nativeBounds = Bounds(baseline.Native);
        foreach (Frame frame in frames)
        {
            frame.ViewerImage = Normalize(frame.Viewer, viewerBounds);
            frame.NativeImage = Normalize(frame.Native, nativeBounds);
            CubismViewerImageMask.KeepLargest(frame.ViewerImage);
            CubismViewerImageMask.KeepLargest(frame.NativeImage);
            frame.ViewerImage.Save(Path.Combine(normalized,
                frame.Name + "-viewer.png"), ImageFormat.Png);
            frame.NativeImage.Save(Path.Combine(normalized,
                frame.Name + "-native.png"), ImageFormat.Png);
        }
        if (seed == 0) seed = Environment.TickCount & Int32.MaxValue;
        Random random = new Random(seed);
        Region[] regions = {
            new Region("full", new Rectangle(0, 0, Canvas, Canvas)),
            new Region("face", new Rectangle(225, 170, 350, 285)),
            new Region("hair", new Rectangle(70, 40, 660, 455)),
            new Region("hands", new Rectangle(55, 395, 690, 315)) };
        string metricsPath = Path.Combine(outputDirectory, "metrics.csv");
        string keyPath = Path.Combine(outputDirectory, "answer-key.csv");
        using (var metrics = new StreamWriter(metricsPath, false))
        using (var key = new StreamWriter(keyPath, false))
        using (var vb = new Pixels(baseline.ViewerImage))
        using (var nb = new Pixels(baseline.NativeImage))
        {
            metrics.WriteLine("frame,region,similarity,within8,foreground_iou," +
                "viewer_change,native_change,response_ratio,samples");
            key.WriteLine("frame,A,B");
            foreach (Frame frame in frames)
            using (var v = new Pixels(frame.ViewerImage))
            using (var n = new Pixels(frame.NativeImage))
            {
                foreach (Region region in regions)
                    Measure(metrics, frame, region, v, n, vb, nb);
                bool viewerFirst = random.Next(2) == 0;
                Ballot(frame, Path.Combine(ballots, frame.Name + ".png"), viewerFirst);
                key.WriteLine(frame.Name + "," +
                    (viewerFirst ? "Viewer,Native" : "Native,Viewer"));
            }
        }
        File.WriteAllText(Path.Combine(outputDirectory, "summary.json"),
            "{\n  \"seed\": " + seed + ",\n  \"frames\": " + frames.Count +
            ",\n  \"canvas\": " + Canvas + "\n}\n");
        foreach (Frame frame in frames) { frame.ViewerImage.Dispose();
            frame.NativeImage.Dispose(); }
        return outputDirectory;
    }
}
