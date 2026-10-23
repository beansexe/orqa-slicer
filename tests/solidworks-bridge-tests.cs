using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using Orqa.SolidWorks;

class BridgeTests
{
    static int checks;
    static void Check(bool value, string name) { if (!value) throw new Exception(name); ++checks; }
    static void Write(string path, object value) {
        string temp = path + ".tmp";
        File.WriteAllText(temp, new JavaScriptSerializer().Serialize(value), new UTF8Encoding(false));
        File.Move(temp, path);
    }
    static async Task Test(string root)
    {
        Check(FastStartPolicy.CubicCentimeters(0.00001) == 10.0, "SolidWorks cubic meters convert to cubic centimeters");
        Check(!FastStartPolicy.NeedsWarning(9.999), "small part does not warn");
        Check(FastStartPolicy.NeedsWarning(10.0), "exact threshold warns");
        Check(FastStartPolicy.NeedsWarning(100.0), "large part warns");
        Check(FastStartPolicy.NeedsWarning(double.NaN) && FastStartPolicy.NeedsWarning(0) && FastStartPolicy.NeedsWarning(double.PositiveInfinity), "unknown or invalid volume cannot bypass warning");
        string discovery = Path.Combine(root, "discovery");
        string endpoint = Path.Combine(discovery, "live", "endpoint.json");
        Directory.CreateDirectory(Path.GetDirectoryName(endpoint));
        Write(endpoint, new { schema = 1, pid = Process.GetCurrentProcess().Id, session = "live-delayed-session", executable = Process.GetCurrentProcess().MainModule.FileName });
        File.SetLastWriteTimeUtc(endpoint, DateTime.UtcNow.AddMinutes(-5));
        var discovered = new BridgeClient { DiagnosticsPath = Path.Combine(root, "discovery-diagnostics.json") };
        discovered.Connect(new[] { discovery, discovery });
        Check(discovered.Session == "live-delayed-session", "live session survives delayed heartbeat and host process-name differences");
        Check(File.Exists(discovered.DiagnosticsPath), "host discovery diagnostics recorded");
        string deadFolder = Path.Combine(root, "dead-discovery", "dead");
        Directory.CreateDirectory(deadFolder);
        Write(Path.Combine(deadFolder, "endpoint.json"), new { schema = 1, pid = int.MaxValue, session = "dead-session", executable = "missing.exe" });
        bool deadRejected = false;
        try { discovered.Connect(new[] { Path.GetDirectoryName(deadFolder) }); }
        catch (InvalidOperationException) { deadRejected = true; }
        Check(deadRejected, "exited slicer session rejected");
        var nativeCatalog = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>("{\"printers\":[{\"id\":\"flashforge:opaque\",\"name\":\"Printer with spaces\"}],\"profiles\":[]}");
        Check(BridgeClient.Rows(nativeCatalog, "printers").Single()["name"].ToString() == "Printer with spaces", "native deserialized array choices");
        Check(!BridgeClient.Rows(nativeCatalog, "profiles").Any(), "native empty array choices");
        var client = new BridgeClient { Root = root, Session = "test-session", RequestTimeout = TimeSpan.FromMilliseconds(350) };
        // A withdrawn request must never become a delayed physical print.
        bool timedOut = false;
        try { await client.Request("print", new Dictionary<string, object>()); } catch (TimeoutException) { timedOut = true; }
        Check(timedOut, "timeout is surfaced");
        Check(Directory.GetFiles(root, "request.json", SearchOption.AllDirectories).Length == 0, "timed-out unclaimed print withdrawn");
        Check(Directory.GetFiles(root, "withdrawn.json", SearchOption.AllDirectories).Length == 1, "no request retry");

        client.RequestTimeout = TimeSpan.FromSeconds(4);
        var server = Task.Run(async () => {
            var until = DateTime.UtcNow.AddSeconds(3);
            while (DateTime.UtcNow < until) {
                var file = Directory.GetFiles(root, "request.json", SearchOption.AllDirectories).FirstOrDefault();
                if (file != null) {
                    string dir = Path.GetDirectoryName(file);
                    File.Move(file, Path.Combine(dir, "processing.json"));
                    var request = BridgeClient.Read(Path.Combine(dir, "processing.json"));
                    Check(BridgeClient.Str(request, "material") == "@current", "unsaved filament choice preserved");
                    Check(BridgeClient.Str(request, "preset") == "@current", "unsaved process choice preserved");
                    Check(BridgeClient.Str(request, "printer") == "bbl:opaque-id", "printer identity preserved");
                    Check(BridgeClient.Str(request, "session") == "test-session", "session supplied");
                    Write(Path.Combine(dir, "response.json"), new { ok = true, result = new { token = Path.GetFileName(dir), background_supported = true } });
                    return;
                }
                await Task.Delay(20);
            }
            throw new Exception("Mock server received no prepare request");
        });
        var prepared = await client.Prepare("bbl:opaque-id", "Exact nozzle profile", "@current", "@current");
        await server;
        Check(prepared.Token == Path.GetFileName(prepared.Directory), "job-local snapshots isolated");

        prepared.FastStartChecked = true;
        var checkedPrint = Task.Run(async () => {
            var until = DateTime.UtcNow.AddSeconds(3);
            while (DateTime.UtcNow < until) {
                var file = Directory.GetFiles(root, "request.json", SearchOption.AllDirectories).FirstOrDefault();
                if (file != null) {
                    string dir = Path.GetDirectoryName(file);
                    File.Move(file, Path.Combine(dir, "checked-print.json"));
                    var request = BridgeClient.Read(Path.Combine(dir, "checked-print.json"));
                    Check(Convert.ToBoolean(request["fast_start_checked"]), "volume check acknowledgement reaches the exact prepared print");
                    Check(BridgeClient.Str(request, "token") == prepared.Token, "warning approval is bound to the prepared snapshot");
                    Write(Path.Combine(dir, "completion.json"), new { ok = true, message = "Mock only" });
                    Write(Path.Combine(dir, "response.json"), new { ok = true, result = new { completion_directory = dir } });
                    return;
                }
                await Task.Delay(10);
            }
            throw new Exception("Mock print request missing");
        });
        Check(await client.Print(prepared, true) == "Mock only", "checked print protocol completes without a real printer");
        await checkedPrint;

        // Claim without acknowledging simulates a lost reply after submission.
        client.RequestTimeout = TimeSpan.FromMilliseconds(350);
        var claimed = Task.Run(async () => {
            while (true) {
                var file = Directory.GetFiles(root, "request.json", SearchOption.AllDirectories).FirstOrDefault();
                if (file != null) { File.Move(file, Path.Combine(Path.GetDirectoryName(file), "processing.json")); return; }
                await Task.Delay(10);
            }
        });
        string message = "";
        try { await client.Request("print", new Dictionary<string, object> { {"token", prepared.Token} }); }
        catch (TimeoutException e) { message = e.Message; }
        await claimed;
        Check(message.Contains("Check Orca and the printer"), "claimed timeout reported as uncertain");
        Check(Directory.GetFiles(root, "processing.json", SearchOption.AllDirectories).Length == 2, "claimed print never replayed");

        // Exercise quoting through the actual Windows process parser, without a shell.
        var values = new[] { "plain", "a b", "trailing slash \\", "embedded\"quote", "a\\\"b", "$(never-run)`literal" };
        var start = new ProcessStartInfo(Process.GetCurrentProcess().MainModule.FileName,
            "--echo " + string.Join(" ", values.Select(BridgeClient.Quote))) {
            UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true };
        using (var process = Process.Start(start)) {
            string result = await process.StandardOutput.ReadToEndAsync(); process.WaitForExit();
            var received = new JavaScriptSerializer().Deserialize<string[]>(result);
            Check(values.SequenceEqual(received), "Unicode/space/quote/backslash arguments round-trip without shell interpretation");
        }
    }
    static int Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "--echo") { Console.Write(new JavaScriptSerializer().Serialize(args.Skip(1).ToArray())); return 0; }
        string root = Path.Combine(Path.GetFullPath("artifacts/solidworks-tests"), Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try { Test(root).GetAwaiter().GetResult(); Console.WriteLine("Passed " + checks + " SolidWorks bridge checks. No slicer or printer contacted."); return 0; }
        catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }
}
