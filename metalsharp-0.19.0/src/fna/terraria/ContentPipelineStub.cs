using System;
using System.Collections.Generic;
using System.Reflection;
[assembly: AssemblyTitle("Microsoft.Xna.Framework.Content.Pipeline")]
[assembly: AssemblyVersion("4.0.0.0")]

namespace Microsoft.Xna.Framework.Content.Pipeline
{
    public enum TargetPlatform { Windows, Xbox360, WindowsPhone, iOS, Android, MacOSX, WindowsStoreApp, NativeClient, WindowsPhone81, MonoAndroid, MonoTouch, NativeClient64 }

    public class OpaqueDataDictionary : Dictionary<string, string> { }

    public class ContentIdentity
    {
        public string SourceFilename { get; set; }
        public string SourceTool { get; set; }
        public int FragmentLineNumber { get; set; }
        public int FragmentColumnNumber { get; set; }
        public ContentIdentity() { }
        public ContentIdentity(string sourceFilename) { SourceFilename = sourceFilename; }
    }

    public abstract class ContentBuildLogger
    {
        public abstract void LogMessage(string message, params object[] messageArgs);
        public abstract void LogImportantMessage(string message, params object[] messageArgs);
        public abstract void LogWarning(string helpLink, ContentIdentity contentIdentity, string message, params object[] messageArgs);
    }

    public abstract class ContentProcessorContext
    {
        public virtual TargetPlatform TargetPlatform { get { return TargetPlatform.Windows; } }
        public virtual Microsoft.Xna.Framework.Graphics.GraphicsProfile TargetProfile { get { return Microsoft.Xna.Framework.Graphics.GraphicsProfile.Reach; } }
        public virtual ContentBuildLogger Logger { get { return null; } }
        public virtual OpaqueDataDictionary Parameters { get { return new OpaqueDataDictionary(); } }
        public virtual string BuildConfiguration { get { return "Release"; } }
        public virtual string OutputFilename { get { return ""; } }
        public virtual string OutputDirectory { get { return ""; } }
        public virtual string IntermediateDirectory { get { return ""; } }
        public virtual void AddDependency(string filename) { }
        public virtual void AddOutputFile(string filename) { }
        public virtual ExternalReference<TOutput> BuildAsset<TInput, TOutput>(ExternalReference<TInput> sourceAsset, string processorName, OpaqueDataDictionary processorParameters, string importerName, string assetName) { return null; }
        public virtual TOutput BuildAndLoadAsset<TInput, TOutput>(ExternalReference<TInput> sourceAsset, string processorName, OpaqueDataDictionary processorParameters, string importerName) { return default(TOutput); }
        public virtual TOutput Convert<TInput, TOutput>(TInput input, string processorName, OpaqueDataDictionary processorParameters) { return default(TOutput); }
    }

    public class ExternalReference
    {
        public string Filename { get; set; }
        public ExternalReference() { }
        public ExternalReference(string filename) { Filename = filename; }
    }
    public class ExternalReference<T> : ExternalReference { }

    public interface IContentImporter { }
    public interface IContentProcessor { }
    public class ContentImporterAttribute : Attribute { public string DefaultProcessor { get; set; } public string DisplayName { get; set; } public string FileExtension { get; set; } }
    public class ContentProcessorAttribute : Attribute { public string DisplayName { get; set; } }
}

namespace Microsoft.Xna.Framework.Content.Pipeline.Graphics
{
    using Microsoft.Xna.Framework.Graphics;

    public class TextureContent { }
    public class Texture2DContent : TextureContent { }
    public class MeshContent { }
    public class NodeContent { }
    public class BoneContent : NodeContent { }
    public class AnimationContent { }
    public class AnimationContentDictionary : Dictionary<string, AnimationContent> { }
    public abstract class MaterialContent { }
    public class EffectMaterialContent : MaterialContent { }
    public class BasicMaterialContent : MaterialContent { }
    public class DualTextureMaterialContent : MaterialContent { }
    public class EnvironmentMapMaterialContent : MaterialContent { }
    public class SkinnedMaterialContent : MaterialContent { }
    public class AlphaTestMaterialContent : MaterialContent { }
    public class EffectContent { public string EffectCode { get; set; } }
    public class CompiledEffectContent { public byte[] GetEffectCode() { return new byte[0]; } }
}

namespace Microsoft.Xna.Framework.Content.Pipeline.Processors
{
    public class EffectProcessorDebugMode { public static readonly EffectProcessorDebugMode Debug = new EffectProcessorDebugMode(); public static readonly EffectProcessorDebugMode Release = new EffectProcessorDebugMode(); }
    public enum MaterialProcessorDefaultEffect { BasicEffect, SkinnedEffect, EnvironmentMapEffect, DualTextureEffect, AlphaTestEffect }
    public enum FontDescriptionStyle { Regular, Bold, Italic }
    
    public class EffectProcessor { }
}
