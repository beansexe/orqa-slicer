using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;

namespace Orqa.SolidWorks
{
    internal static class FastStartPolicy
    {
        internal static double CubicCentimeters(double cubicMeters) { return cubicMeters * 1000000.0; }
        internal static bool NeedsWarning(double cm3) {
            return double.IsNaN(cm3) || double.IsInfinity(cm3) || cm3 <= 0 || cm3 >= 10.0;
        }
    }
    internal sealed class Job
    {
        public string Directory, Token;
        public Dictionary<string, object> Settings;
        public bool FastStartChecked = false;
    }

    internal sealed class BridgeClient
    {
        internal static readonly JavaScriptSerializer Json = new JavaScriptSerializer { MaxJsonLength = 1024 * 1024 };
        internal string Root, Session, Executable;
        internal int ProcessId;
        internal string DiagnosticsPath;
        internal TimeSpan RequestTimeout = TimeSpan.FromSeconds(30);
        internal static Dictionary<string, object> Read(string path)
        { return Json.Deserialize<Dictionary<string, object>>(File.ReadAllText(path, Encoding.UTF8)); }
        internal static string Str(Dictionary<string, object> value, string key)
        { object result; return value.TryGetValue(key, out result) && result != null ? Convert.ToString(result) : ""; }
        internal static Dictionary<string, object> Map(object value)
        { return (Dictionary<string, object>)value; }
        internal static IEnumerable<Dictionary<string, object>> Rows(Dictionary<string, object> value, string key)
        { return ((System.Collections.IEnumerable)value[key]).Cast<object>().Select(Map); }

        internal void Connect()
        {
            Connect(new[] { Environment.GetEnvironmentVariable("LOCALAPPDATA"),
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData) }
                .Where(p => !string.IsNullOrWhiteSpace(p)).Select(p => Path.Combine(p, "OrqaSolidWorks", "bridge")));
        }

        internal void Connect(IEnumerable<string> folders)
        {
            var endpoints = new List<Tuple<DateTime, string, Dictionary<string, object>>>();
            var observations = new List<object>();
            foreach (string folder in folders.Distinct(StringComparer.OrdinalIgnoreCase))
            {
            observations.Add(new { folder = folder, exists = System.IO.Directory.Exists(folder) });
            if (System.IO.Directory.Exists(folder))
                foreach (string endpoint in System.IO.Directory.GetDirectories(folder).Select(d => Path.Combine(d, "endpoint.json")))
                    try
                    {
                        var value = Read(endpoint);
                        int pid = Convert.ToInt32(value["pid"]);
                        using (var process = Process.GetProcessById(pid))
                        {
                            if (process.HasExited) continue;
                        }
                        // The local session token and request/response handshake identify
                        // the server. A UI host may observe a delayed timer or a different
                        // process-name representation; neither makes a live endpoint invalid.
                        if (Convert.ToInt32(value["schema"]) != 1) continue;
                        if (string.IsNullOrEmpty(Str(value, "session")) || string.IsNullOrEmpty(Str(value, "executable"))) continue;
                        endpoints.Add(Tuple.Create(File.GetLastWriteTimeUtc(endpoint), endpoint, value));
                        observations.Add(new { endpoint = endpoint, pid = pid, discovered = true });
                    }
                    catch (ArgumentException) { }
                    catch (IOException) { }
                    catch (InvalidOperationException) { }
            }
            // A short Windows path and its long form can refer to the same session.
            endpoints = endpoints.GroupBy(e => Str(e.Item3, "pid") + ":" + Str(e.Item3, "session")).Select(g => g.First()).ToList();
            if (!string.IsNullOrEmpty(DiagnosticsPath))
                try { File.WriteAllText(DiagnosticsPath, Json.Serialize(new { utc = DateTime.UtcNow.ToString("o"), candidates = endpoints.Count, observations = observations })); }
                catch (IOException) { }
            if (endpoints.Count == 0)
                throw new InvalidOperationException("No running Orqa bridge session was found. Open slicer, then click Refresh. Discovery details were saved beside the add-in.");
            if (endpoints.Count > 1)
                throw new InvalidOperationException("More than one bridge-enabled Orca is running. Close the extra instance so the target session is unambiguous.");
            var chosen = endpoints[0];
            Root = Path.GetDirectoryName(chosen.Item2);
            Session = Str(chosen.Item3, "session"); Executable = Str(chosen.Item3, "executable");
            ProcessId = Convert.ToInt32(chosen.Item3["pid"]);
        }

        [DllImport("user32.dll")]
        static extern bool AllowSetForegroundWindow(int processId);
        [DllImport("user32.dll")]
        static extern bool IsIconic(IntPtr window);
        [DllImport("user32.dll")]
        static extern bool ShowWindowAsync(IntPtr window, int command);
        [DllImport("user32.dll")]
        static extern bool SetForegroundWindow(IntPtr window);

        internal void PermitSlicerActivation()
        {
            // Called synchronously from the user's Load click, while SolidWorks
            // owns foreground permission. Orca may then raise itself on import.
            if (ProcessId > 0) AllowSetForegroundWindow(ProcessId);
        }

        internal bool ActivateSlicer()
        {
            try {
                using (var process = Process.GetProcessById(ProcessId)) {
                    process.Refresh();
                    IntPtr window = process.MainWindowHandle;
                    if (window == IntPtr.Zero) return false;
                    if (IsIconic(window)) ShowWindowAsync(window, 9); // SW_RESTORE
                    return SetForegroundWindow(window);
                }
            }
            catch (ArgumentException) { return false; }
            catch (InvalidOperationException) { return false; }
            catch (System.ComponentModel.Win32Exception) { return false; }
        }

        internal async Task<Dictionary<string, object>> Request(string op, Dictionary<string, object> args, string directory = null)
        {
            if (Root == null) Connect();
            string id = Guid.NewGuid().ToString("N");
            string dir = directory ?? Path.Combine(Root, "requests", id);
            System.IO.Directory.CreateDirectory(dir);
            args["schema"] = 1; args["session"] = Session; args["op"] = op;
            // Publish a complete request atomically. There are no automatic retries.
            string temp = Path.Combine(dir, "request.tmp");
            File.WriteAllText(temp, Json.Serialize(args), new UTF8Encoding(false));
            File.Move(temp, Path.Combine(dir, "request.json"));
            string response = Path.Combine(dir, "response.json");
            var deadline = DateTime.UtcNow.Add(op == "load" ? TimeSpan.FromMinutes(5) : RequestTimeout);
            while (!File.Exists(response))
            {
                if (DateTime.UtcNow > deadline) {
                    bool withdrawn = false;
                    try { File.Move(Path.Combine(dir, "request.json"), Path.Combine(dir, "withdrawn.json")); withdrawn = true; } catch (IOException) { }
                    throw new TimeoutException(withdrawn ? "Orca did not respond. The request was withdrawn before processing; nothing was sent. Close any modal dialog in Orca and refresh." :
                        "Orca has started processing but did not return a result. Check Orca and the printer before retrying. No automatic retry was made.");
                }
                await Task.Delay(150);
            }
            var answer = Read(response);
            if (!Convert.ToBoolean(answer["ok"])) throw new InvalidOperationException(Str(answer, "message"));
            var result = Map(answer["result"]);
            if (op == "print") result["completion_directory"] = dir;
            return result;
        }

        internal async Task<Job> Prepare(string printer, string profile, string material, string preset)
        {
            string dir = Path.Combine(Root, "requests", Guid.NewGuid().ToString("N"));
            var result = await Request("prepare", new Dictionary<string, object> {
                {"printer", printer}, {"profile", profile}, {"material", material}, {"preset", preset} }, dir);
            return new Job { Directory = dir, Token = Str(result, "token"), Settings = result };
        }

        // Windows command-line quoting, including literal quotes and trailing backslashes.
        internal static string Quote(string value)
        {
            var result = new StringBuilder("\""); int slashes = 0;
            foreach (char c in value)
            {
                if (c == '\\') { ++slashes; continue; }
                if (c == '"') result.Append('\\', slashes * 2 + 1).Append('"');
                else result.Append('\\', slashes).Append(c);
                slashes = 0;
            }
            return result.Append('\\', slashes * 2).Append('"').ToString();
        }

        internal async Task Slice(Job job)
        {
            var arguments = new[] { "--load-settings", Path.Combine(job.Directory, "machine.json") + ";" + Path.Combine(job.Directory, "process.json"),
                "--load-filaments", Path.Combine(job.Directory, "filament.json"), "--slice", "0", "--arrange", "1", "--orient", "1", "--strict",
                "--export-3mf", "plate.3mf", "--outputdir", job.Directory,
                Path.Combine(job.Directory, "model.step") };
            var start = new ProcessStartInfo(Executable, string.Join(" ", arguments.Select(Quote))) {
                UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden,
                WorkingDirectory = Path.GetDirectoryName(Executable), RedirectStandardOutput = true, RedirectStandardError = true };
            // The CLI has its own temporary datadir. It cannot persist over live presets/accounts.
            start.Arguments = "--datadir " + Quote(Path.Combine(job.Directory, "cli-data")) + " " + start.Arguments;
            using (var process = Process.Start(start))
            {
                Task<string> stdout = process.StandardOutput.ReadToEndAsync();
                Task<string> stderr = process.StandardError.ReadToEndAsync();
                var exit = Task.Run(() => process.WaitForExit());
                if (await Task.WhenAny(exit, Task.Delay(TimeSpan.FromMinutes(10))) != exit) {
                    process.Kill(); await exit;
                    throw new TimeoutException("Slicing exceeded ten minutes and was stopped. Nothing was submitted. Load in slicer to inspect the part.");
                }
                string output = await stdout, error = await stderr;
                File.WriteAllText(Path.Combine(job.Directory, "slice.log"), output + "\r\n" + error);
                if (process.ExitCode != 0 || !File.Exists(Path.Combine(job.Directory, "plate.3mf")))
                    throw new InvalidOperationException("Background slicing failed (" + process.ExitCode + "). Load in slicer to inspect orientation, supports and settings. Details: " + Path.Combine(job.Directory, "slice.log"));
            }
            using (var archive = ZipFile.OpenRead(Path.Combine(job.Directory, "plate.3mf")))
            {
                var image = archive.GetEntry("Metadata/plate_1.png");
                if (image == null || image.Length == 0 || image.Length > 8 * 1024 * 1024)
                    throw new InvalidOperationException("The sliced file has no valid plate preview. Load in slicer to review it.");
                image.ExtractToFile(Path.Combine(job.Directory, "thumbnail.png"));
            }
        }

        internal async Task<string> Print(Job job, bool leveling)
        {
            var answer = await Request("print", new Dictionary<string, object> { {"token", job.Token}, {"leveling", leveling}, {"external_spool", true}, {"fast_start_checked", job.FastStartChecked} });
            string dir = Str(answer, "completion_directory");
            var deadline = DateTime.UtcNow.AddMinutes(15);
            string completion = Path.Combine(dir, "completion.json");
            while (!File.Exists(completion))
            {
                if (DateTime.UtcNow > deadline) throw new TimeoutException("The delivery result is uncertain. Check the printer in Orca Device before retrying. No automatic retry was made.");
                await Task.Delay(500);
            }
            var result = Read(completion);
            if (!Convert.ToBoolean(result["ok"])) throw new InvalidOperationException(Str(result, "message"));
            return Str(result, "message");
        }
    }
}
