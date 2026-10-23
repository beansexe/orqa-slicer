using System;
using System.IO;
using System.Reflection;
using System.Windows.Forms;
class WarningPreview {
    [STAThread]
    static int Main(string[] args) {
        // Invokes the production dialog only. No COM, bridge, slicing or printer calls.
        Application.EnableVisualStyles();
        if (args.Length == 0) args = new[] {
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "solidworks-0.1.0.16", "Orqa.SolidWorks.Bridge16.dll"),
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "fast-start-dialog-result.txt") };
        var assembly = Assembly.LoadFrom(Path.GetFullPath(args[0]));
        var method = assembly.GetType("Orqa.SolidWorks.FastStartWarning").GetMethod("Confirm", BindingFlags.Static | BindingFlags.NonPublic);
        bool result = (bool)method.Invoke(null, new object[] { null, "Bambu Lab X1 Carbon 0.4 nozzle - Fast Start", 10.0 });
        File.WriteAllText(args[1], result ? "Print anyway" : "Cancelled");
        return 0;
    }
}
