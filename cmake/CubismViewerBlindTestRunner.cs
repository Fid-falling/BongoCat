using System;

internal static class CubismViewerBlindTestRunner
{
    public static int Main(string[] args)
    {
        if (args.Length < 3)
        {
            Console.Error.WriteLine("usage: blind-test.exe viewer native output [seed]");
            return 2;
        }
        int seed = args.Length > 3 ? Int32.Parse(args[3]) : 20260807;
        try
        {
            Console.WriteLine(CubismViewerBlindTest.Run(
                args[0], args[1], args[2], seed));
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
