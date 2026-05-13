using System;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;

class TerrariaLauncher {
    static Assembly _ta;
    static int _charReceived = 0;
    
    static int Main(string[] args) {
        AppDomain.CurrentDomain.UnhandledException += (s, e) => {
            Console.Error.WriteLine("[SUPPRESSED] " + e.ExceptionObject);
        };
        
        _ta = Assembly.LoadFrom("Terraria.exe");
        AppDomain.CurrentDomain.AssemblyResolve += OnResolve;
        
        using (var stream = _ta.GetManifestResourceStream("Terraria.Libraries.ReLogic.ReLogic.dll")) {
            byte[] data = new byte[stream.Length];
            stream.Read(data, 0, data.Length);
            Assembly r = Assembly.Load(data);
            Type pt = r.GetType("ReLogic.OS.Platform");
            Type ot = r.GetType("ReLogic.OS.OSX.OsxPlatform");
            pt.GetField("Current", BindingFlags.Static | BindingFlags.Public)
              .SetValue(null, Activator.CreateInstance(ot, true));
        }
        
        _ta.GetType("Terraria.Program").GetField("LoadedEverything", BindingFlags.Static | BindingFlags.Public).SetValue(null, true);
        
        ThreadPool.QueueUserWorkItem(_ => {
            Thread.Sleep(8000);
            try { BridgeTextInput(); }
            catch (Exception ex) { Console.Error.WriteLine("Bridge: " + ex.Message); }
        });
        
        Type lt = _ta.GetType("Terraria.WindowsLaunch");
        MethodInfo mm = null;
        foreach (MethodInfo m in lt.GetMethods(BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic)) {
            if (m.Name == "Main" && m.GetParameters().Length == 1) { mm = m; break; }
        }
        
        try { mm.Invoke(null, new object[] { args }); }
        catch (TargetInvocationException tie) { Console.Error.WriteLine("Game: " + tie.InnerException?.Message); }
        return 0;
    }
    
    static void BridgeTextInput() {
        Type mainType = _ta.GetType("Terraria.Main");
        var flags = BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
        
        // Dump actual field types
        foreach (var f in mainType.GetFields(flags)) {
            if (f.Name == "keyInt" || f.Name == "keyString" || f.Name == "keyCount") {
                Console.Error.WriteLine("Bridge: " + f.Name + " = " + f.FieldType.FullName + (f.IsPublic ? " public" : " private"));
            }
        }
        
        FieldInfo keyIntField = mainType.GetField("keyInt", flags);
        FieldInfo keyStringField = mainType.GetField("keyString", flags);
        FieldInfo keyCountField = mainType.GetField("keyCount", flags);
        
        // Get game instance
        FieldInfo instanceField = mainType.GetField("instance", BindingFlags.Static | BindingFlags.Public);
        object gameInstance = null;
        for (int i = 0; i < 100; i++) { Thread.Sleep(200); gameInstance = instanceField.GetValue(null); if (gameInstance != null) break; }
        if (gameInstance == null) { Console.Error.WriteLine("Bridge: no instance"); return; }
        
        IntPtr windowHandle = (IntPtr)gameInstance.GetType().BaseType.GetProperty("Window").GetValue(gameInstance).GetType().GetProperty("Handle").GetValue(
            gameInstance.GetType().BaseType.GetProperty("Window").GetValue(gameInstance));
        
        // Start text input
        Type textInputExt = null;
        foreach (Assembly asm in AppDomain.CurrentDomain.GetAssemblies()) {
            if (asm.GetName().Name == "FNA") { textInputExt = asm.GetType("Microsoft.Xna.Framework.Input.TextInputEXT"); break; }
        }
        textInputExt.GetProperty("WindowHandle").SetValue(null, windowHandle);
        SDL_StartTextInput(windowHandle);
        
        // Now read the ACTUAL runtime types of the fields
        object keyIntVal = keyIntField.GetValue(null);
        object keyStringVal = keyStringField.GetValue(null);
        object keyCountVal = keyCountField.GetValue(null);
        Console.Error.WriteLine("Bridge runtime: keyInt=" + keyIntVal.GetType().FullName + " keyString=" + keyStringVal.GetType().FullName + " keyCount type=" + keyCountVal.GetType().FullName + " keyCount val=" + keyCountVal);
        
        // Subscribe - use object/Convert to handle whatever type keyCount is
        EventInfo ev = textInputExt.GetEvent("TextInput");
        Action<char> handler = c => {
            Interlocked.Increment(ref _charReceived);
            try {
                object ki = keyIntField.GetValue(null);
                object ks = keyStringField.GetValue(null);
                object kc = keyCountField.GetValue(null);
                int count = Convert.ToInt32(kc);
                
                // keyInt is int[] not char[] ?
                if (ki is int[] intArr && count < intArr.Length) {
                    intArr[count] = (int)c;
                    if (ks is string[] strArr) strArr[count] = c.ToString();
                    keyCountField.SetValue(null, count + 1);
                } else if (ki is char[] charArr && count < charArr.Length) {
                    charArr[count] = c;
                    if (ks is string[] strArr) strArr[count] = c.ToString();
                    keyCountField.SetValue(null, count + 1);
                }
            } catch (Exception ex) { Console.Error.WriteLine("Bridge err: " + ex.GetType().Name + ": " + ex.Message); }
        };
        ev.AddEventHandler(null, handler);
        Console.Error.WriteLine("Bridge: installed!");
    }
    
    [DllImport("SDL3", CallingConvention = CallingConvention.Cdecl)]
    static extern byte SDL_StartTextInput(IntPtr window);
    
    static Assembly OnResolve(object sender, ResolveEventArgs e) {
        string sn = new AssemblyName(e.Name).Name;
        foreach (string res in _ta.GetManifestResourceNames()) {
            if (res.EndsWith(sn + ".dll")) {
                try { using (var s = _ta.GetManifestResourceStream(res)) { if (s != null) { byte[] d = new byte[s.Length]; s.Read(d, 0, d.Length); return Assembly.Load(d); } } } catch {}
            }
        }
        return null;
    }
}
