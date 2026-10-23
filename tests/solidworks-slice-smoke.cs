using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using Orqa.SolidWorks;
class SliceSmoke
{
    static int Main(string[] args)
    {
        try {
            string directory = Path.GetFullPath(args[0]);
            var client = new BridgeClient { Executable = Path.GetFullPath(args[1]) };
            client.Slice(new Job { Directory = directory }).GetAwaiter().GetResult();
            using (var zip = ZipFile.OpenRead(Path.Combine(directory, "plate.3mf"))) {
                foreach (var name in new[] { "Metadata/plate_1.gcode", "Metadata/plate_1.png", "Metadata/slice_info.config", "Metadata/model_settings.config" }) {
                    var entry = zip.GetEntry(name);
                    if (entry == null || entry.Length == 0) throw new Exception("Missing archive content: " + name);
                }
                using (var reader = new StreamReader(zip.GetEntry("Metadata/slice_info.config").Open()))
                    if (!reader.ReadToEnd().Contains("value=\"Flashforge-Adventurer-5M\""))
                        throw new Exception("Native printer catalog ID was not exported");
            }
            Console.WriteLine("Passed real background STEP -> sliced 3MF, G-code, metadata and preview checks. No print submitted.");
            return 0;
        } catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }
}
