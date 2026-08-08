using System;
using System.Drawing;

public static class CubismViewerImageMask
{
    public static void KeepLargest(Bitmap image)
    {
        int width = image.Width, height = image.Height, size = width * height;
        int[] labels = new int[size], queue = new int[size];
        int[] counts = new int[size], cyanCounts = new int[size];
        int label = 0, bestLabel = 0, bestCount = 0;
        int bestLeft = 0, bestTop = 0, bestRight = width - 1,
            bestBottom = height - 1;
        for (int start = 0; start < size; ++start)
        {
            int sx = start % width, sy = start / width;
            Color pixel = image.GetPixel(sx, sy);
            if (labels[start] != 0 || Math.Min(pixel.R,
                Math.Min(pixel.G, pixel.B)) >= 237) continue;
            int head = 0, tail = 0, left = width, top = height;
            int right = -1, bottom = -1, cyan = 0; label++;
            queue[tail++] = start; labels[start] = label;
            while (head < tail)
            {
                int current = queue[head++], x = current % width;
                int y = current / width;
                Color currentColor = image.GetPixel(x, y);
                left = Math.Min(left, x); top = Math.Min(top, y);
                right = Math.Max(right, x); bottom = Math.Max(bottom, y);
                if (currentColor.R < 32 && currentColor.G > 220 &&
                    currentColor.B > 220) cyan++;
                int[] next = { current - 1, current + 1,
                    current - width, current + width };
                for (int i = 0; i < next.Length; ++i)
                {
                    int value = next[i];
                    if (value < 0 || value >= size || labels[value] != 0 ||
                        (i == 0 && x == 0) ||
                        (i == 1 && x == width - 1)) continue;
                    Color candidate = image.GetPixel(value % width,
                        value / width);
                    if (Math.Min(candidate.R, Math.Min(candidate.G,
                        candidate.B)) >= 237) continue;
                    labels[value] = label; queue[tail++] = value;
                }
            }
            counts[label] = tail; cyanCounts[label] = cyan;
            if (tail > bestCount) { bestCount = tail; bestLabel = label;
                bestLeft = left; bestTop = top; bestRight = right;
                bestBottom = bottom; }
        }
        for (int index = 0; index < size; ++index)
        {
            int currentLabel = labels[index], x = index % width, y = index / width;
            bool outside = x < bestLeft || x > bestRight ||
                y < bestTop || y > bestBottom;
            bool cyan = currentLabel != 0 &&
                cyanCounts[currentLabel] * 10 > counts[currentLabel];
            if (currentLabel != 0 && currentLabel != bestLabel &&
                (outside || cyan)) image.SetPixel(x, y, Color.White);
        }
    }
}
