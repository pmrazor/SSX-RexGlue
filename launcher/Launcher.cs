// Native GUI entry point: host Windows PowerShell's options form in-process.
// No console, shell command construction, child shell or extra runtime download.
using System;
using System.IO;
using System.Management.Automation;
using System.Management.Automation.Runspaces;
using System.Threading;
using System.Windows.Forms;

internal static class Launcher {
    [STAThread]
    private static int Main(string[] args) {
        Application.EnableVisualStyles();
        try {
            string script = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "launcher", "Options.ps1");
            if (!File.Exists(script)) throw new FileNotFoundException("Missing launcher/Options.ps1", script);
            if (args.Length != 0 && (args.Length != 2 || args[0] != "--settings"))
                throw new ArgumentException("Usage: SSX.Options.exe [--settings path.json]");
            var state = InitialSessionState.CreateDefault();
            // Applies to this embedded session only, never machine/user policy.
            state.ExecutionPolicy = Microsoft.PowerShell.ExecutionPolicy.Bypass;
            using (var runspace = RunspaceFactory.CreateRunspace(state)) {
                runspace.ApartmentState = ApartmentState.STA;
                runspace.ThreadOptions = PSThreadOptions.UseCurrentThread;
                runspace.Open();
                using (var powershell = PowerShell.Create()) {
                    powershell.Runspace = runspace;
                    powershell.AddCommand(script);
                    if (args.Length == 2) powershell.AddParameter("SettingsPath", Path.GetFullPath(args[1]));
                    powershell.Invoke();
                    if (powershell.HadErrors) throw new InvalidOperationException(powershell.Streams.Error[0].ToString());
                }
            }
            return 0;
        } catch (Exception error) {
            MessageBox.Show(error.Message, "SSX Options", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
    }
}
