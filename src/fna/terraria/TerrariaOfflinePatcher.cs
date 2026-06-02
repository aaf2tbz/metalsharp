using System;
using System.IO;
using System.Linq;
using Mono.Cecil;
using Mono.Cecil.Cil;

class TerrariaOfflinePatcher
{
    static int Main(string[] args)
    {
        if (args.Length != 1) return 2;

        string path = args[0];
        string backup = path + ".metalsharp-original";
        if (!File.Exists(backup)) File.Copy(path, backup);

        AssemblyDefinition asm = AssemblyDefinition.ReadAssembly(path);
        ModuleDefinition module = asm.MainModule;

        PatchSocialDefault(module);
        PatchForceLoader(module);

        string patchedPath = path + ".metalsharp-patched";
        if (File.Exists(patchedPath)) File.Delete(patchedPath);
        asm.Write(patchedPath);
        File.Delete(path);
        File.Move(patchedPath, path);
        return 0;
    }

    static void PatchSocialDefault(ModuleDefinition module)
    {
        TypeDefinition socialApi = module.Types.First(t => t.FullName == "Terraria.Social.SocialAPI");
        MethodDefinition init = socialApi.Methods.First(m => m.Name == "Initialize" && m.Parameters.Count == 1);
        Instruction target = init.Body.Instructions.FirstOrDefault(i =>
            i.OpCode == OpCodes.Call &&
            i.Operand is MethodReference method &&
            method.Name == "get_Value" &&
            method.DeclaringType.FullName.StartsWith("System.Nullable`1<Terraria.Social.SocialMode>"));
        if (target == null) throw new InvalidOperationException("SocialAPI.Initialize get_Value not found");

        ILProcessor il = init.Body.GetILProcessor();
        for (int i = 0; i < init.Body.Instructions.Count - 1; i++)
        {
            Instruction instruction = init.Body.Instructions[i];
            if (instruction.OpCode == OpCodes.Call &&
                instruction.Operand is MethodReference method &&
                method.Name == ".ctor" &&
                method.DeclaringType.FullName.StartsWith("System.Nullable`1<Terraria.Social.SocialMode>"))
            {
                Instruction next = init.Body.Instructions[i + 1];
                if (next.OpCode != OpCodes.Br && next.OpCode != OpCodes.Br_S)
                    il.InsertAfter(instruction, il.Create(OpCodes.Br, target));
                return;
            }
        }

        throw new InvalidOperationException("SocialAPI.Initialize default branch not found");
    }

    static void PatchForceLoader(ModuleDefinition module)
    {
        TypeDefinition program = module.Types.First(t => t.FullName == "Terraria.Program");
        MethodDefinition startForceLoad = program.Methods.FirstOrDefault(m => m.Name == "StartForceLoad");
        if (startForceLoad == null) return;

        startForceLoad.Body.Instructions.Clear();
        startForceLoad.Body.ExceptionHandlers.Clear();
        startForceLoad.Body.Variables.Clear();
        startForceLoad.Body.InitLocals = false;
        startForceLoad.Body.Instructions.Add(Instruction.Create(OpCodes.Ret));
    }
}
