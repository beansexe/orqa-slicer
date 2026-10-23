using System;
using System.Collections.Generic;
using System.Linq;
using System.IO;
using Orqa.SolidWorks;
class NativeSessionSmoke
{
    static int Main()
    {
        try {
            Run().GetAwaiter().GetResult(); return 0;
        } catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }
    static async System.Threading.Tasks.Task Run()
    {
        var client = new BridgeClient(); client.Connect();
        var catalog = await client.Request("catalog", new Dictionary<string, object>());
        var printers = BridgeClient.Rows(catalog, "printers").ToArray();
        Console.WriteLine("Live native mailbox responded; connected printer count: " + printers.Length);
        string actual = client.Session;
        client.Session = "expired-test-session";
        bool rejected = false;
        try { await client.Request("catalog", new Dictionary<string, object>()); }
        catch (InvalidOperationException e) { rejected = e.Message.Contains("session changed"); }
        if (!rejected) throw new Exception("Stale session accepted");
        client.Session = actual;
        rejected = false;
        try { await client.Request("unknown-test-operation", new Dictionary<string, object>()); }
        catch (InvalidOperationException e) { rejected = e.Message.Contains("Unsupported"); }
        if (!rejected) throw new Exception("Unknown operation accepted");
        rejected = false;
        try { await client.Prepare("bbl:nonexistent-test-device", "missing", "@current", "@current"); }
        catch (InvalidOperationException e) { rejected = e.Message.Contains("disconnected"); }
        if (!rejected) throw new Exception("Missing printer accepted");
        foreach (var printer in printers) {
            string id = BridgeClient.Str(printer, "id");
            var details = await client.Request("catalog", new Dictionary<string, object> { {"printer", id} });
            var profiles = BridgeClient.Rows(details, "profiles").ToArray();
            if (profiles.Length == 0) { Console.WriteLine("A connected printer has no enabled matching profile; selection correctly unavailable."); continue; }
            string profile = BridgeClient.Str(profiles.FirstOrDefault(p => BridgeClient.Str(p, "id").Contains("0.4")) ?? profiles[0], "id");
            details = await client.Request("catalog", new Dictionary<string, object> { {"printer", id}, {"profile", profile} });
            var materials = BridgeClient.Rows(details, "materials").ToArray();
            var presets = BridgeClient.Rows(details, "presets").ToArray();
            if (materials.Length == 0 || presets.Length == 0) throw new Exception("Matching printer has no compatible materials/processes");
            var job = await client.Prepare(id, profile, BridgeClient.Str(materials[0], "id"), BridgeClient.Str(presets[0], "id"));
            foreach (var type in new[] { "machine", "process", "filament" }) {
                var snapshot = BridgeClient.Read(Path.Combine(job.Directory, type + ".json"));
                if (BridgeClient.Str(snapshot, "type") != type || BridgeClient.Str(snapshot, "inherits") != "") throw new Exception("Invalid resolved snapshot");
                if (snapshot.ContainsKey("printhost_apikey") || snapshot.ContainsKey("printhost_password")) throw new Exception("Network secret exported");
            }
            Console.WriteLine("Live printer profile, " + materials.Length + " material choices and " + presets.Length + " process choices exported as temporary resolved snapshots.");
        }
        Console.WriteLine("Native session/schema routing, stale-session, unsupported-operation and missing-printer checks passed. No model loaded and no print submitted.");
    }
}
