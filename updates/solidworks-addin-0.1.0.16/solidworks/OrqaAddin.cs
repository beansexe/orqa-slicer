using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using SolidWorks.Interop.swpublished;

namespace Orqa.SolidWorks
{
    internal static class FastStartWarning
    {
        internal static bool Confirm(IWin32Window owner, string profile, double volume)
        {
            using (var warning = new Form { Text = "Review Fast Start settings", Width = 560, Height = 340, Font = SystemFonts.MessageBoxFont,
                StartPosition = owner == null ? FormStartPosition.CenterScreen : FormStartPosition.CenterParent,
                FormBorderStyle = FormBorderStyle.FixedDialog, ShowInTaskbar = false, MaximizeBox = false, MinimizeBox = false })
            {
                string amount = double.IsNaN(volume) || double.IsInfinity(volume) || volume <= 0 ?
                    "The solid model volume could not be determined." :
                    "Solid model volume: " + volume.ToString("0.00") + " cm³ (warning at 10 cm³).";
                warning.Controls.Add(new Label { Left = 18, Top = 18, Width = 510, Height = 220,
                    Text = "Fast Start is selected: " + profile + "\r\n\r\n" + amount +
                    "\r\n\r\nFast Start skips startup checks and material loading/purge. For a finished part, cancel and select the normal machine profile." });
                var cancel = new Button { Text = "Cancel", Left = 295, Top = 260, Width = 100, DialogResult = DialogResult.Cancel };
                var proceed = new Button { Text = "Print anyway", Left = 405, Top = 260, Width = 125, DialogResult = DialogResult.OK };
                warning.Controls.Add(cancel); warning.Controls.Add(proceed);
                warning.AcceptButton = cancel; warning.CancelButton = cancel; warning.ActiveControl = cancel;
                return warning.ShowDialog(owner) == DialogResult.OK;
            }
        }
    }
    [ComVisible(true), Guid("9D99577C-4D62-43C3-8656-95FD6B483928"), InterfaceType(ComInterfaceType.InterfaceIsIDispatch)]
    public interface IOrqaCommands
    {
        void ChooseAndPrint();
        void LoadInSlicer();
        int EnablePart();
    }

    [ComVisible(true), Guid("4FF7418C-716D-42C2-881C-D6652C61D9DC"), ProgId("Orqa.SolidWorks.Addin16"),
     ClassInterface(ClassInterfaceType.None), ComDefaultInterface(typeof(IOrqaCommands))]
    public sealed class OrqaAddin : ISwAddin, IOrqaCommands
    {
        const int GroupId = 927410;
        SldWorks app;
        ICommandManager manager;
        ICommandGroup group;
        ExportDialog dialog;
        internal static string InstallDirectory { get { return Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location); } }

        public bool ConnectToSW(object ThisSW, int Cookie)
        {
            try
            {
                app = (SldWorks)ThisSW;
                app.SetAddinCallbackInfo2(0, this, Cookie);
                manager = app.GetCommandManager(Cookie);
                int error = 0;
                group = manager.CreateCommandGroup2(GroupId, "Orqa", "Export the active part to your connected printers", "Orqa", -1, false, ref error);
                int print = group.AddCommandItem2("Choose and print", -1, "Choose printer, filament and process, then print in the background", "Choose and print", 0,
                    "ChooseAndPrint", "EnablePart", 1, (int)(swCommandItemType_e.swMenuItem | swCommandItemType_e.swToolbarItem));
                int load = group.AddCommandItem2("Load in slicer", -1, "Export the active part and edit it in Orca", "Load in slicer", 1,
                    "LoadInSlicer", "EnablePart", 2, (int)(swCommandItemType_e.swMenuItem | swCommandItemType_e.swToolbarItem));
                group.SmallIconList = Path.Combine(InstallDirectory, "commands16.bmp");
                group.LargeIconList = Path.Combine(InstallDirectory, "commands24.bmp");
                group.SmallMainIcon = Path.Combine(InstallDirectory, "main16.bmp");
                group.LargeMainIcon = Path.Combine(InstallDirectory, "main24.bmp");
                group.HasToolbar = true; group.HasMenu = true; group.Activate();
                var tab = manager.GetCommandTab((int)swDocumentTypes_e.swDocPART, "Orqa");
                if (tab != null) manager.RemoveCommandTab(tab);
                tab = manager.AddCommandTab((int)swDocumentTypes_e.swDocPART, "Orqa");
                var box = tab.AddCommandTabBox();
                box.AddCommands(new int[] { group.CommandID[print], group.CommandID[load] },
                    new int[] { (int)swCommandTabButtonTextDisplay_e.swCommandTabButton_TextBelow, (int)swCommandTabButtonTextDisplay_e.swCommandTabButton_TextBelow });
                return true;
            }
            catch (Exception e) { MessageBox.Show(e.Message, "Orqa add-in"); return false; }
        }
        public bool DisconnectFromSW()
        {
            if (dialog != null && !dialog.IsDisposed) dialog.Close();
            if (manager != null) manager.RemoveCommandGroup(GroupId);
            group = null; manager = null; app = null;
            return true;
        }
        public int EnablePart()
        {
            var document = app == null ? null : app.IActiveDoc2;
            return document != null && document.GetType() == (int)swDocumentTypes_e.swDocPART ? 1 : 0;
        }
        public void ChooseAndPrint() { Show(false); }
        public void LoadInSlicer() { Show(true); }
        void Show(bool load)
        {
            if (EnablePart() == 0) return;
            if (dialog != null && !dialog.IsDisposed) { dialog.Activate(); return; }
            var owner = new WindowOwner(new IntPtr(app.IFrameObject().GetHWnd()));
            dialog = new ExportDialog(app, load, owner);
            dialog.Show(owner);
        }
    }
    internal sealed class WindowOwner : IWin32Window
    {
        public IntPtr Handle { get; private set; }
        internal WindowOwner(IntPtr value) { Handle = value; }
    }
    internal sealed class Option
    {
        internal string Id, Name;
        public override string ToString() { return Name; }
    }
    internal sealed class ExportDialog : Form
    {
        static string lastPrinter = "", lastProfile = "", lastMaterial = "", lastPreset = "";
        readonly SldWorks app;
        readonly WindowOwner solidworksOwner;
        readonly BridgeClient bridge = new BridgeClient();
        readonly ComboBox printers = Choice(), profiles = Choice(), materials = Choice(), presets = Choice();
        readonly Button refresh = new Button { Text = "Refresh", AutoSize = true }, open = new Button { Text = "Open slicer", AutoSize = true },
            load = new Button { Text = "Load in slicer", AutoSize = true }, print = new Button { Text = "Print", AutoSize = true };
        readonly CheckBox leveling = new CheckBox { Text = "Level bed before printing", Checked = true, AutoSize = true },
            external = new CheckBox { Text = "The selected material is loaded on the external spool (background print)", AutoSize = true };
        readonly Label status = new Label { AutoSize = false, Height = 90, Dock = DockStyle.Fill };
        bool updating, busy, unresolved, handoffToSlicer;
        readonly bool preferLoad;
        string lastSource;
        static ComboBox Choice() { return new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill, Width = 470, DropDownWidth = 850 }; }
        static string Id(ComboBox box) { return box.SelectedItem is Option ? ((Option)box.SelectedItem).Id : ""; }
        static void Fill(ComboBox box, Dictionary<string, object> data, string key, string previous)
        {
            box.Items.Clear();
            foreach (var row in BridgeClient.Rows(data, key)) box.Items.Add(new Option { Id = BridgeClient.Str(row, "id"), Name = BridgeClient.Str(row, "name") });
            for (int i = 0; i < box.Items.Count; ++i) if (((Option)box.Items[i]).Id == previous) { box.SelectedIndex = i; return; }
            if (box.Items.Count == 1 || (key != "profiles" && box.Items.Count > 0)) box.SelectedIndex = 0;
        }
        internal ExportDialog(SldWorks solidworks, bool preferLoad, WindowOwner owner)
        {
            app = solidworks; this.preferLoad = preferLoad; solidworksOwner = owner;
            bridge.DiagnosticsPath = Path.Combine(OrqaAddin.InstallDirectory, "connection-diagnostics.json");
            Text = "Orqa — active SolidWorks part"; Width = 720; Height = 560; MinimumSize = new Size(720, 560);
            StartPosition = FormStartPosition.CenterParent; Font = SystemFonts.MessageBoxFont;
            ShowInTaskbar = false; MinimizeBox = false;
            var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(16), ColumnCount = 2, RowCount = 9 };
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 160)); layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            string[] labels = { "Printer", "Nozzle profile", "Material", "Process preset" };
            ComboBox[] boxes = { printers, profiles, materials, presets };
            for (int i = 0; i < boxes.Length; ++i) { layout.Controls.Add(new Label { Text = labels[i], AutoSize = true, Anchor = AnchorStyles.Left }, 0, i); layout.Controls.Add(boxes[i], 1, i); layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 38)); }
            layout.Controls.Add(leveling, 1, 4); layout.Controls.Add(external, 0, 5); layout.SetColumnSpan(external, 2);
            var help = new Label { Text = "Current in Orca reuses unsaved process or filament edits. Load in slicer for orientation, supports, AMS or multiple extruders.", AutoSize = true, MaximumSize = new Size(590, 0), Margin = new Padding(0, 12, 0, 10) };
            layout.Controls.Add(help, 0, 6); layout.SetColumnSpan(help, 2);
            var actions = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, WrapContents = false };
            actions.Controls.AddRange(new Control[] { refresh, open, load, print });
            layout.Controls.Add(actions, 0, 7); layout.SetColumnSpan(actions, 2);
            layout.Controls.Add(status, 0, 8); layout.SetColumnSpan(status, 2); layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            Controls.Add(layout);
            var tip = new ToolTip(); tip.SetToolTip(print, "Exports the active part as STEP, slices without opening an Orca window, and submits through Orca's connected account.");
            tip.SetToolTip(load, "Exports the active part and opens it in Orca with your selected settings.");
            refresh.Click += async (s, e) => await RefreshCatalog(true);
            printers.SelectedIndexChanged += async (s, e) => { if (!updating) {
                updating = true; profiles.Items.Clear(); materials.Items.Clear(); presets.Items.Clear(); updating = false;
                await RefreshCatalog(false);
            } };
            profiles.SelectedIndexChanged += async (s, e) => { if (!updating) await RefreshCatalog(false); };
            materials.SelectedIndexChanged += (s, e) => UpdateButtons(); presets.SelectedIndexChanged += (s, e) => UpdateButtons();
            load.Click += async (s, e) => await Run(false);
            print.Click += async (s, e) => await Run(true);
            external.CheckedChanged += (s, e) => UpdateButtons();
            open.Click += (s, e) => {
                try {
                    string launch = Path.Combine(OrqaAddin.InstallDirectory, "workspace.txt");
                    string workspace = File.ReadAllText(launch).Trim();
                    Process.Start(new ProcessStartInfo(Path.Combine(workspace, "Launch-Slicer.cmd")) { UseShellExecute = true, WindowStyle = ProcessWindowStyle.Hidden });
                    status.Text = "When Orca is ready and connected, click Refresh.";
                } catch (Exception error) { status.Text = error.Message; }
            };
            Shown += async (s, e) => await RefreshCatalog(true);
            FormClosing += (s, e) => { if (busy) { e.Cancel = true; status.Text = "Wait for this operation to finish. Closing the dialog cannot recall a submitted print."; } };
            FormClosed += (s, e) => {
                if (handoffToSlicer) return;
                // Run after WinForms finishes closing the owned window, so its
                // own activation cleanup cannot select a different application.
                var context = SynchronizationContext.Current;
                IntPtr frame = solidworksOwner.Handle;
                if (context != null) context.Post(ignored => BridgeClient.ActivateWindow(frame), null);
                else BridgeClient.ActivateWindow(frame);
            };
            AcceptButton = preferLoad ? load : print; UpdateButtons();
        }
        void UpdateButtons()
        {
            bool selected = Id(printers) != "" && Id(profiles) != "" && Id(materials) != "" && Id(presets) != "";
            load.Enabled = !busy && selected; print.Enabled = !busy && !unresolved && selected && external.Checked;
            refresh.Enabled = open.Enabled = !busy;
            foreach (var box in new[] { printers, profiles, materials, presets }) box.Enabled = !busy;
        }
        async Task RefreshCatalog(bool reconnect)
        {
            if (busy) return;
            busy = true; updating = true; UpdateButtons();
            try
            {
                if (reconnect) bridge.Connect();
                string printer = Id(printers), profile = Id(profiles), material = Id(materials), preset = Id(presets);
                if (printers.Items.Count == 0) { printer = lastPrinter; profile = lastProfile; material = lastMaterial; preset = lastPreset; }
                // Refresh identities before asking for dependent choices. A printer
                // or profile removed since the dialog opened must not trap Refresh
                // in repeated requests for the stale identity.
                var data = await bridge.Request("catalog", new Dictionary<string, object>());
                Fill(printers, data, "printers", printer);
                if (Id(printers) != printer) profile = "";
                printer = Id(printers);
                data = await bridge.Request("catalog", new Dictionary<string, object> { {"printer", printer} });
                Fill(profiles, data, "profiles", profile);
                profile = Id(profiles);
                if (profile != "") data = await bridge.Request("catalog", new Dictionary<string, object> { {"printer", printer}, {"profile", profile} });
                Fill(materials, data, "materials", material); Fill(presets, data, "presets", preset);
                status.Text = printers.Items.Count == 0 ? "No connected account printers were reported. Connect or refresh them in Orca." :
                    profile == "" ? "Choose the installed nozzle profile." : "Ready. The model will use the active SolidWorks configuration.";
            }
            catch (Exception e) { status.Text = e.Message; }
            finally { busy = false; updating = false; UpdateButtons(); }
        }
        string Export(Job job, bool doPrint)
        {
            var document = app.IActiveDoc2;
            if (document == null || document.GetType() != (int)swDocumentTypes_e.swDocPART) throw new InvalidOperationException("Activate a SolidWorks part first.");
            string configuration = document.ConfigurationManager.ActiveConfiguration.Name;
            string identity = document.GetPathName();
            if (string.IsNullOrEmpty(identity)) identity = "unsaved:" + document.GetTitle() + ":" + document.GetHashCode();
            string source;
            using (var sha = SHA256.Create()) source = BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(identity + "|" + configuration))).Replace("-", "").ToLowerInvariant();
            string models = Path.Combine(bridge.Root, "models"); System.IO.Directory.CreateDirectory(models);
            string destination = Path.Combine(models, source + ".step"), staging = Path.Combine(job.Directory, "model.step");
            var selection = document.ISelectionManager;
            var selected = new List<object>();
            for (int i = 1; i <= selection.GetSelectedObjectCount2(-1); ++i) selected.Add(selection.GetSelectedObject6(i, -1));
            document.ClearSelection2(true);
            int errors = 0, warnings = 0;
            try
            {
                if (doPrint && Convert.ToBoolean(job.Settings["fast_start"]))
                {
                    var mass = document.Extension.CreateMassProperty();
                    double volume = double.NaN;
                    if (mass != null) { mass.UseSystemUnits = true; volume = FastStartPolicy.CubicCentimeters(mass.Volume); }
                    if (FastStartPolicy.NeedsWarning(volume))
                    {
                        if (!FastStartWarning.Confirm(this, Id(profiles), volume)) {
                            status.Text = "Print cancelled. Review the machine profile before printing."; return null;
                        }
                    }
                    job.FastStartChecked = true;
                }
                bool saved = document.Extension.SaveAs(staging, (int)swSaveAsVersion_e.swSaveAsCurrentVersion,
                    (int)swSaveAsOptions_e.swSaveAsOptions_Silent, null, ref errors, ref warnings);
                if (!saved || errors != 0 || !File.Exists(staging) || new FileInfo(staging).Length == 0)
                    throw new IOException("STEP export failed. SolidWorks error " + errors + ", warning " + warnings + ".");
            }
            finally { if (selected.Count > 0) document.Extension.MultiSelect2(selected.ToArray(), false, null); }
            // The stable file replaces the prior export; request-local files isolate an in-flight slice.
            string temp = destination + ".tmp"; File.Copy(staging, temp, true);
            if (File.Exists(destination)) File.Replace(temp, destination, null); else File.Move(temp, destination);
            return source;
        }
        async Task Run(bool doPrint)
        {
            if (busy) return;
            bool loaded = false;
            if (!doPrint) bridge.PermitSlicerActivation();
            busy = true; UpdateButtons();
            try
            {
                status.Text = "Reading the selected settings from Orca…";
                lastPrinter = Id(printers); lastProfile = Id(profiles); lastMaterial = Id(materials); lastPreset = Id(presets);
                var job = await bridge.Prepare(Id(printers), Id(profiles), Id(materials), Id(presets));
                if (doPrint && !job.Settings.ContainsKey("fast_start"))
                    throw new InvalidOperationException("Open the updated Orqa slicer revision to enable the Fast Start volume check.");
                if (doPrint && !Convert.ToBoolean(job.Settings["background_supported"]))
                    throw new InvalidOperationException("This profile has multiple extruders. Use Load in slicer to review nozzle and material routing, then print there.");
                status.Text = "Exporting the active part as STEP…";
                lastSource = Export(job, doPrint);
                if (lastSource == null) return;
                if (!doPrint)
                {
                    status.Text = "Loading in Orca…";
                    var answer = await bridge.Request("load", new Dictionary<string, object> { {"token", job.Token}, {"source", lastSource} });
                    status.Text = BridgeClient.Str(answer, "message");
                    loaded = true;
                }
                else
                {
                    status.Text = "Slicing in the background…";
                    await bridge.Slice(job);
                    status.Text = "Submitting through Orca's cloud connection…";
                    unresolved = true; // Remains latched if delivery becomes uncertain.
                    status.Text = await bridge.Print(job, leveling.Checked);
                    unresolved = false;
                }
            }
            catch (Exception e) { status.Text = e.Message; }
            finally {
                busy = false;
                if (loaded) {
                    // Closing an owned dialog can reactivate SolidWorks. Close it
                    // first, then hand focus to the exact Orca session used above.
                    handoffToSlicer = true;
                    Close();
                    bridge.ActivateSlicer();
                } else UpdateButtons();
            }
        }
    }
}
