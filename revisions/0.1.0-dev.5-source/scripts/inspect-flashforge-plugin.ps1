$ErrorActionPreference = 'Stop'
# Read only the installed plugin's public ABI version. No initialization or login.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FlashforgePluginProbe {
    [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32", CharSet=CharSet.Ansi)]
    static extern IntPtr GetProcAddress(IntPtr library, string name);
    [DllImport("kernel32")] static extern bool FreeLibrary(IntPtr library);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate IntPtr VersionFunction();
    public static string Version(string path) {
        IntPtr library = LoadLibraryEx(path, IntPtr.Zero, 0x100 | 0x1000);
        if (library == IntPtr.Zero) throw new InvalidOperationException("Plugin could not load: " + Marshal.GetLastWin32Error());
        try {
            IntPtr address = GetProcAddress(library, "fnet_getVersion");
            if (address == IntPtr.Zero) throw new InvalidOperationException("Version export unavailable");
            var function = (VersionFunction)Marshal.GetDelegateForFunctionPointer(address, typeof(VersionFunction));
            return Marshal.PtrToStringAnsi(function());
        } finally { FreeLibrary(library); }
    }
}
'@
[FlashforgePluginProbe]::Version('C:\Program Files\Flashforge\Orca-Flashforge\FlashNetwork.dll')
