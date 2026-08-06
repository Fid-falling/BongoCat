using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public sealed class MverPhaseCandidate
{
    public int NativeOffsetFrames { get; set; }
    public double NativeOffsetSeconds { get; set; }
    public int OverlapFrames { get; set; }
    public double FullSimilarity { get; set; }
    public double FaceSimilarity { get; set; }
    public double HairSimilarity { get; set; }
    public double HandSimilarity { get; set; }
    public double FullMotionSimilarity { get; set; }
    public double FaceMotionSimilarity { get; set; }
    public double HairMotionSimilarity { get; set; }
    public double HandMotionSimilarity { get; set; }
}

public static class MverPhaseMetrics
{
    private sealed class Frame
    {
        public int Width;
        public int Height;
        public int GridWidth;
        public int GridHeight;
        public byte[] Bgr;
    }

    private sealed class Score
    {
        public readonly long[] Absolute = new long[4];
        public readonly long[] Samples = new long[4];
    }

    private sealed class MotionScore
    {
        public readonly double[] Dot = new double[4];
        public readonly double[] FirstSquared = new double[4];
        public readonly double[] SecondSquared = new double[4];
    }

    private const int PixelStep = 4;

    private static Frame Load(string path)
    {
        using (var source = new Bitmap(path))
        using (var bitmap = new Bitmap(source.Width, source.Height,
            PixelFormat.Format32bppArgb))
        {
            using (Graphics graphics = Graphics.FromImage(bitmap))
            {
                graphics.CompositingMode = CompositingMode.SourceCopy;
                graphics.DrawImageUnscaled(source, 0, 0);
            }
            var rect = new Rectangle(0, 0, bitmap.Width, bitmap.Height);
            BitmapData locked = bitmap.LockBits(rect, ImageLockMode.ReadOnly,
                PixelFormat.Format32bppArgb);
            int stride = Math.Abs(locked.Stride);
            var sourceBytes = new byte[stride * bitmap.Height];
            Marshal.Copy(locked.Scan0, sourceBytes, 0, sourceBytes.Length);
            bitmap.UnlockBits(locked);
            int gridWidth = (bitmap.Width + PixelStep - 1) / PixelStep;
            int gridHeight = (bitmap.Height + PixelStep - 1) / PixelStep;
            var bgr = new byte[gridWidth * gridHeight * 3];
            for (int gy = 0; gy < gridHeight; ++gy)
            for (int gx = 0; gx < gridWidth; ++gx)
            {
                int sourceOffset = Math.Min(bitmap.Height - 1, gy * PixelStep) * stride +
                    Math.Min(bitmap.Width - 1, gx * PixelStep) * 4;
                int outputOffset = (gy * gridWidth + gx) * 3;
                bgr[outputOffset] = sourceBytes[sourceOffset];
                bgr[outputOffset + 1] = sourceBytes[sourceOffset + 1];
                bgr[outputOffset + 2] = sourceBytes[sourceOffset + 2];
            }
            return new Frame { Width = bitmap.Width, Height = bitmap.Height,
                GridWidth = gridWidth, GridHeight = gridHeight, Bgr = bgr };
        }
    }

    private static Frame[] LoadAll(string[] paths)
    {
        var result = new Frame[paths.Length];
        for (int i = 0; i < paths.Length; ++i) result[i] = Load(paths[i]);
        for (int i = 1; i < result.Length; ++i)
            if (result[i].Width != result[0].Width ||
                result[i].Height != result[0].Height)
                throw new InvalidOperationException("Frame dimensions differ");
        return result;
    }

    private static bool InRegion(int region, int x, int y, int width, int height)
    {
        double nx = x / (double)width;
        double ny = y / (double)height;
        if (region == 0) return true;
        if (region == 1) return nx >= .27 && nx <= .74 && ny >= .24 && ny <= .67;
        if (region == 2) return ny <= .36 ||
            ((nx <= .30 || nx >= .71) && ny <= .70);
        return nx >= .16 && nx <= .58 && ny >= .58 && ny <= .92;
    }

    private static void AddPair(Score score, Frame first, Frame second,
        int spatialStride)
    {
        if (first.Width != second.Width || first.Height != second.Height)
            throw new InvalidOperationException("Frame dimensions differ");
        for (int gy = 0; gy < first.GridHeight; gy += spatialStride)
        for (int gx = 0; gx < first.GridWidth; gx += spatialStride)
        {
            int offset = (gy * first.GridWidth + gx) * 3;
            int firstMaximum = Math.Max(first.Bgr[offset],
                Math.Max(first.Bgr[offset + 1], first.Bgr[offset + 2]));
            int secondMaximum = Math.Max(second.Bgr[offset],
                Math.Max(second.Bgr[offset + 1], second.Bgr[offset + 2]));
            if (firstMaximum <= 4 && secondMaximum <= 4) continue;
            int absolute = Math.Abs(first.Bgr[offset] - second.Bgr[offset]) +
                Math.Abs(first.Bgr[offset + 1] - second.Bgr[offset + 1]) +
                Math.Abs(first.Bgr[offset + 2] - second.Bgr[offset + 2]);
            int x = gx * PixelStep;
            int y = gy * PixelStep;
            for (int region = 0; region < 4; ++region)
            {
                if (!InRegion(region, x, y, first.Width, first.Height)) continue;
                score.Absolute[region] += absolute;
                score.Samples[region]++;
            }
        }
    }

    private static double Similarity(Score score, int region)
    {
        return score.Samples[region] == 0 ? 0.0 : 100.0 *
            (1.0 - score.Absolute[region] / (score.Samples[region] * 3.0 * 255.0));
    }

    private static void AddMotionPair(MotionScore score,
        Frame firstPrevious, Frame first, Frame secondPrevious, Frame second,
        int spatialStride)
    {
        for (int gy = 0; gy < first.GridHeight; gy += spatialStride)
        for (int gx = 0; gx < first.GridWidth; gx += spatialStride)
        {
            int offset = (gy * first.GridWidth + gx) * 3;
            int x = gx * PixelStep;
            int y = gy * PixelStep;
            for (int channel = 0; channel < 3; ++channel)
            {
                double firstDelta = first.Bgr[offset + channel] -
                    firstPrevious.Bgr[offset + channel];
                double secondDelta = second.Bgr[offset + channel] -
                    secondPrevious.Bgr[offset + channel];
                if (firstDelta == 0.0 && secondDelta == 0.0) continue;
                for (int region = 0; region < 4; ++region)
                {
                    if (!InRegion(region, x, y, first.Width, first.Height)) continue;
                    score.Dot[region] += firstDelta * secondDelta;
                    score.FirstSquared[region] += firstDelta * firstDelta;
                    score.SecondSquared[region] += secondDelta * secondDelta;
                }
            }
        }
    }

    private static double MotionSimilarity(MotionScore score, int region)
    {
        double denominator = Math.Sqrt(score.FirstSquared[region] *
            score.SecondSquared[region]);
        if (denominator <= 0.0) return 0.0;
        double correlation = Math.Max(-1.0, Math.Min(1.0,
            score.Dot[region] / denominator));
        return 50.0 * (correlation + 1.0);
    }

    private static MverPhaseCandidate Candidate(Score score, MotionScore motion,
        int offset, int overlap, int intervalMilliseconds)
    {
        return new MverPhaseCandidate {
            NativeOffsetFrames = offset,
            NativeOffsetSeconds = offset * intervalMilliseconds / 1000.0,
            OverlapFrames = overlap,
            FullSimilarity = Similarity(score, 0),
            FaceSimilarity = Similarity(score, 1),
            HairSimilarity = Similarity(score, 2),
            HandSimilarity = Similarity(score, 3),
            FullMotionSimilarity = MotionSimilarity(motion, 0),
            FaceMotionSimilarity = MotionSimilarity(motion, 1),
            HairMotionSimilarity = MotionSimilarity(motion, 2),
            HandMotionSimilarity = MotionSimilarity(motion, 3)
        };
    }

    private static MverPhaseCandidate Measure(Frame[] mver, Frame[] native,
        int offset, int intervalMilliseconds, int spatialStride)
    {
        int mverStart = Math.Max(0, -offset);
        int nativeStart = Math.Max(0, offset);
        int overlap = Math.Min(mver.Length - mverStart,
            native.Length - nativeStart);
        var score = new Score();
        var motion = new MotionScore();
        for (int frame = 0; frame < overlap; frame += 2)
            AddPair(score, mver[mverStart + frame],
                native[nativeStart + frame], spatialStride);
        for (int frame = 1; frame < overlap; ++frame)
            AddMotionPair(motion, mver[mverStart + frame - 1],
                mver[mverStart + frame], native[nativeStart + frame - 1],
                native[nativeStart + frame], spatialStride);
        return Candidate(score, motion, offset, overlap, intervalMilliseconds);
    }

    public static MverPhaseCandidate[] Search(string[] mverPaths,
        string[] nativePaths, int maxLagFrames, int minimumOverlapFrames,
        int intervalMilliseconds)
    {
        if (maxLagFrames < 0 || minimumOverlapFrames < 2 ||
            mverPaths.Length < minimumOverlapFrames ||
            nativePaths.Length < minimumOverlapFrames)
            throw new ArgumentException("Insufficient frames for lag analysis");
        Frame[] mver = LoadAll(mverPaths);
        Frame[] native = LoadAll(nativePaths);
        var candidates = new List<MverPhaseCandidate>(maxLagFrames * 2 + 1);
        for (int offset = -maxLagFrames; offset <= maxLagFrames; ++offset)
        {
            MverPhaseCandidate candidate = Measure(mver, native, offset,
                intervalMilliseconds, 3);
            if (candidate.OverlapFrames >= minimumOverlapFrames)
                candidates.Add(candidate);
        }
        if (candidates.Count == 0)
            throw new ArgumentException("No lag candidate has enough overlap");
        candidates.Sort(delegate(MverPhaseCandidate left, MverPhaseCandidate right) {
            return right.FullMotionSimilarity.CompareTo(left.FullMotionSimilarity);
        });
        var fineCandidates = new List<MverPhaseCandidate>();
        var offsets = new HashSet<int>();
        for (int index = 0; index < Math.Min(7, candidates.Count); ++index)
            offsets.Add(candidates[index].NativeOffsetFrames);
        offsets.Add(0);
        foreach (int offset in offsets)
        {
            MverPhaseCandidate candidate = Measure(mver, native, offset,
                intervalMilliseconds, 1);
            if (candidate.OverlapFrames >= minimumOverlapFrames)
                fineCandidates.Add(candidate);
        }
        fineCandidates.Sort(delegate(MverPhaseCandidate left, MverPhaseCandidate right) {
            return right.FullMotionSimilarity.CompareTo(left.FullMotionSimilarity);
        });
        return fineCandidates.ToArray();
    }
}
