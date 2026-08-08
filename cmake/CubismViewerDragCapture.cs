using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

internal static class CubismViewerDragCapture
{
    const uint MouseLeftDown = 0x0002, MouseLeftUp = 0x0004;
    const uint KeyUp = 0x0002;
    const uint SetWindowShow = 0x0040;
    static readonly IntPtr TopMost = new IntPtr(-1);

    [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(
        IntPtr value);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr window);
    [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr window,
        IntPtr deviceContext);
    [DllImport("gdi32.dll", SetLastError = true)] static extern bool BitBlt(IntPtr destination,
        int x, int y, int width, int height, IntPtr source, int sourceX,
        int sourceY, uint operation);
    [DllImport("user32.dll")] static extern void mouse_event(
        uint flags, uint x, uint y, uint data, UIntPtr extra);
    [DllImport("user32.dll")] static extern void keybd_event(
        byte virtualKey, byte scanCode, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h, int command);
    [DllImport("user32.dll")] static extern bool SetWindowPos(
        IntPtr h, IntPtr insertAfter, int x, int y, int width, int height,
        uint flags);
    const uint SourceCopy = 0x00CC0020;

    static void Click()
    {
        mouse_event(MouseLeftDown, 0, 0, 0, UIntPtr.Zero);
        mouse_event(MouseLeftUp, 0, 0, 0, UIntPtr.Zero);
    }

    static void Key(byte virtualKey)
    {
        keybd_event(virtualKey, 0, 0, UIntPtr.Zero);
        keybd_event(virtualKey, 0, KeyUp, UIntPtr.Zero);
        Thread.Sleep(80);
    }

    static void SelectExpression(int index)
    {
        // The fixed 1280x720 Viewer layout keeps Expressions on this row.
        SetCursorPos(100, 194);
        Click();
        Thread.Sleep(150);
        Key(0x27);
        Thread.Sleep(150);
        SetCursorPos(120, 222 + index * 29);
        Click();
        Thread.Sleep(1000);
    }

    static void FitAndCenter()
    {
        SetCursorPos(313, 659);
        Click();
        Thread.Sleep(300);
        SetCursorPos(426, 659);
        Click();
        Thread.Sleep(500);
    }

    sealed class Frame
    {
        public string Name;
        public double RequestedMilliseconds;
        public double BeginMilliseconds;
        public double EndMilliseconds;
        public Bitmap Image;
    }

    static Bitmap Capture(int left, int top, int width, int height,
        Stopwatch clock, out double begin, out double end)
    {
        Bitmap image = new Bitmap(width, height, PixelFormat.Format32bppArgb);
        using (Graphics graphics = Graphics.FromImage(image))
        {
            IntPtr destination = graphics.GetHdc();
            IntPtr source = GetDC(IntPtr.Zero);
            begin = clock == null ? 0.0 : clock.Elapsed.TotalMilliseconds;
            bool copied = BitBlt(destination, 0, 0, width, height, source,
                left, top, SourceCopy);
            end = clock == null ? 0.0 : clock.Elapsed.TotalMilliseconds;
            ReleaseDC(IntPtr.Zero, source); graphics.ReleaseHdc(destination);
            if (!copied) throw new System.ComponentModel.Win32Exception(
                Marshal.GetLastWin32Error(), "Unable to capture Viewer pixels");
        }
        return image;
    }

    static void WaitUntil(Stopwatch clock, double milliseconds)
    {
        while (clock.Elapsed.TotalMilliseconds < milliseconds)
        {
            double remaining = milliseconds - clock.Elapsed.TotalMilliseconds;
            if (remaining > 2.0) Thread.Sleep(1);
            else Thread.SpinWait(2000);
        }
    }

    static void AddFrame(List<Frame> frames, string name, Stopwatch clock,
        double requestedMilliseconds, int left, int top, int width, int height)
    {
        double begin, end;
        Bitmap image = Capture(left, top, width, height, clock,
            out begin, out end);
        frames.Add(new Frame { Name = name,
            RequestedMilliseconds = requestedMilliseconds,
            BeginMilliseconds = begin, EndMilliseconds = end, Image = image });
    }

    static void SaveFrames(List<Frame> frames, string directory, int pressX,
        int pressY, int targetX, int targetY, int left, int top, int width,
        int height)
    {
        using (StreamWriter trace = new StreamWriter(
            Path.Combine(directory, "trace.csv"), false))
        {
            double dragX = 2.0 * (targetX - left) / width - 1.0;
            double dragY = 1.0 - 2.0 * (targetY - top) / height;
            trace.WriteLine("frame,phase_ms,capture_mid_ms,requested_ms," +
                "capture_end_ms," +
                "press_x,press_y,target_x,target_y,target_drag_x,target_drag_y," +
                "capture_left,capture_top,capture_width,capture_height");
            foreach (Frame frame in frames)
            {
                frame.Image.Save(Path.Combine(directory, frame.Name + ".png"),
                    ImageFormat.Png);
                trace.WriteLine(String.Join(",", frame.Name,
                    frame.BeginMilliseconds.ToString("F3",
                        CultureInfo.InvariantCulture),
                    ((frame.BeginMilliseconds + frame.EndMilliseconds) * 0.5)
                        .ToString("F3", CultureInfo.InvariantCulture),
                    frame.RequestedMilliseconds.ToString("F3",
                        CultureInfo.InvariantCulture),
                    frame.EndMilliseconds.ToString("F3",
                        CultureInfo.InvariantCulture),
                    pressX, pressY, targetX, targetY,
                    dragX.ToString("F6", CultureInfo.InvariantCulture),
                    dragY.ToString("F6", CultureInfo.InvariantCulture),
                    left, top, width, height));
                frame.Image.Dispose();
            }
        }
    }

    public static int Main(string[] args)
    {
        SetProcessDpiAwarenessContext(new IntPtr(-4));
        if (args.Length < 2)
        {
            Console.Error.WriteLine("usage: capture.exe output-directory viewer-pid " +
                "[left top width height press-x press-y target-x target-y " +
                "press-settle-ms expression-index]");
            return 2;
        }
        string directory = Path.GetFullPath(args[0]);
        Directory.CreateDirectory(directory);
        int pid = Int32.Parse(args[1], CultureInfo.InvariantCulture);
        int left = args.Length > 2 ? Int32.Parse(args[2]) : 142;
        int top = args.Length > 3 ? Int32.Parse(args[3]) : 80;
        int width = args.Length > 4 ? Int32.Parse(args[4]) : 1138;
        int height = args.Length > 5 ? Int32.Parse(args[5]) : 640;
        int pressX = args.Length > 6 ? Int32.Parse(args[6]) : 700;
        int pressY = args.Length > 7 ? Int32.Parse(args[7]) : 500;
        int targetX = args.Length > 8 ? Int32.Parse(args[8]) : 985;
        int targetY = args.Length > 9 ? Int32.Parse(args[9]) : 308;
        int pressSettleMilliseconds = args.Length > 10
            ? Int32.Parse(args[10], CultureInfo.InvariantCulture) : 150;
        int expressionIndex = args.Length > 11
            ? Int32.Parse(args[11], CultureInfo.InvariantCulture) : -1;
        Process viewer = Process.GetProcessById(pid);
        IntPtr handle = viewer.MainWindowHandle;
        ShowWindow(handle, 9);
        SetWindowPos(handle, TopMost, 0, 0, 1280, 720, SetWindowShow);
        SetForegroundWindow(handle);
        Thread.Sleep(1200);
        if (expressionIndex >= 0) SelectExpression(expressionIndex);
        FitAndCenter();
        SetCursorPos(pressX, pressY);
        Thread.Sleep(800);
        List<Frame> frames = new List<Frame>();
        try
        {
            AddFrame(frames, "idle", null, 0.0,
                left, top, width, height);
            mouse_event(MouseLeftDown, 0, 0, 0, UIntPtr.Zero);
            Thread.Sleep(pressSettleMilliseconds);
            AddFrame(frames, "track-000", null, 0.0,
                left, top, width, height);
            SetCursorPos(targetX, targetY);
            Stopwatch track = Stopwatch.StartNew();
            string[] names = { "track-001", "track-002", "track-004",
                "track-008", "track-015", "track-030" };
            double[] times = { 16.667, 33.333, 66.667, 133.333, 250.0, 500.0 };
            for (int i = 0; i < names.Length; ++i)
            {
                WaitUntil(track, times[i]);
                AddFrame(frames, names[i], track, times[i],
                    left, top, width, height);
            }
            mouse_event(MouseLeftUp, 0, 0, 0, UIntPtr.Zero);
            Stopwatch release = Stopwatch.StartNew();
            string[] returnNames = { "return-001", "return-002", "return-004",
                "return-008", "return-015", "return-030" };
            for (int i = 0; i < returnNames.Length; ++i)
            {
                WaitUntil(release, times[i]);
                AddFrame(frames, returnNames[i], release, times[i],
                    left, top, width, height);
            }
        }
        finally
        {
            mouse_event(MouseLeftUp, 0, 0, 0, UIntPtr.Zero);
        }
        SaveFrames(frames, directory, pressX, pressY, targetX, targetY,
            left, top, width, height);
        Console.WriteLine("frames={0} output={1}", frames.Count, directory);
        return 0;
    }
}
