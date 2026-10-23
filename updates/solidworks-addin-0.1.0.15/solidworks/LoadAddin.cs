using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Web.Script.Serialization;
using SolidWorks.Interop.sldworks;
using Orqa.SolidWorks;
class OrqaAddinLoader
{
    [STAThread]
    static int Main(string[] args)
    {
        try {
            string folder = AppDomain.CurrentDomain.BaseDirectory;
            var app = (ISldWorks)Marshal.GetActiveObject("SldWorks.Application");
            if (args.Length == 2 && args[0] == "--unload") {
                int unloaded = app.UnloadAddIn(args[1]);
                if (unloaded != 0) throw new Exception("SolidWorks could not unload the earlier add-in: " + unloaded);
                Console.WriteLine("Earlier add-in unloaded. The part was not saved or exported.");
                return 0;
            }
            var before = app.IActiveDoc2;
            string path = before == null ? "" : before.GetPathName();
            bool dirty = before != null && before.GetSaveFlag();
            int result = app.LoadAddIn(Path.Combine(folder, "Orqa.SolidWorks.Bridge15.dll"));
            var addin = (IOrqaCommands)app.GetAddInObject("Orqa.SolidWorks.Addin15");
            if (addin == null) throw new Exception("SolidWorks did not expose the add-in (load code " + result + ").");
            var after = app.IActiveDoc2;
            if (path != (after == null ? "" : after.GetPathName()) || dirty != (after != null && after.GetSaveFlag()))
                throw new Exception("Active document identity or modified flag changed.");
            var status = new { loaded = true, loadCode = result, partCommandsEnabled = addin.EnablePart() == 1,
                documentIdentityAndModifiedFlagPreserved = true };
            File.WriteAllText(Path.Combine(folder, "load-status.json"), new JavaScriptSerializer().Serialize(status));
            Console.WriteLine("Orqa add-in loaded; active document identity and modified flag preserved.");
            return 0;
        } catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }
}
