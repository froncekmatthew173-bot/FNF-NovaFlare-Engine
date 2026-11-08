package originfunkin;

import flixel.FlxG;
import funkin.ui.debug.FunkinDebugDisplay;
import haxe.Exception;
import haxe.io.Path;
import openfl.utils.AssetLibrary;
import openfl.utils.AssetManifest;
import openfl.utils.Assets;
import openfl.utils.AssetType;
import openfl.text.Font;

#if mobile
import general.shaders.MobileShaderConverter;
#end

#if sys
import sys.FileSystem;
#end

class OriginFunkinMode
{
	public static inline final FOLDER_NAME:String = "OriginFunkin";
	public static inline final ASSET_FOLDER_NAME:String = "assets";
	static inline final LEGACY_FOLDER_NAME:String = "originFunkin";
	public static inline final VERSION:String = "0.8.7";

	public static var active(default, null):Bool = false;
	public static var assetsAvailable(default, null):Bool = false;
	public static var assetsRoot(default, null):Null<String>;
	public static var preparationError(default, null):Null<String>;
	public static var mountedAssetCount(default, null):Int = 0;
	public static var mountedLibraries(default, null):Array<String> = [];
	public static var novaFlareIntroVideoPath(default, null):Null<String>;
	public static var noticeFontName(default, null):Null<String>;
	public static var debugDisplay:FunkinDebugDisplay;
	public static var registeredFontCount(default, null):Int = 0;

	static inline final NOVAFLARE_INTRO_ASSET:String = "assets/videos/menuExtend/titleIntro.mp4";
	static inline final NOVAFLARE_INTRO_LIBRARY_ASSET:String = "videos:assets/videos/menuExtend/titleIntro.mp4";
	static inline final NOVAFLARE_NOTICE_FONT_ASSET:String = "assets/fonts/Lang-ZH.ttf";

	static final NAMED_LIBRARIES:Array<String> = [
		"shared",
		"songs",
		"tutorial",
		"week1",
		"week2",
		"week3",
		"week4",
		"week5",
		"week6",
		"week7",
		"weekend1",
		"sserafim",
		"videos"
	];

	public static function detect():Bool
	{
		active = false;
		assetsAvailable = false;
		assetsRoot = null;
		preparationError = null;
		novaFlareIntroVideoPath = null;
		noticeFontName = null;

		#if sys
		try
		{
			OriginFunkinConfig.load();
			var candidate:Null<String> = locateAssetsRoot();
			if (candidate != null)
			{
				validateRequiredPaths(candidate);
				assetsRoot = candidate;
				assetsAvailable = true;
				active = OriginFunkinConfig.shouldStartOrigin();
			}
		}
		catch (error:Dynamic)
		{
			active = false;
			assetsAvailable = false;
			assetsRoot = null;
			preparationError = 'Unable to use the $FOLDER_NAME folder: ${describeError(error)}';
			trace('[originFunkin] $preparationError Falling back to NovaFlare Engine.');
		}
		#end

		return active;
	}

	public static function canEnterOrigin():Bool
	{
		#if sys
		try
		{
			var candidate:Null<String> = locateAssetsRoot();
			if (candidate == null)
			{
				throw 'The "$FOLDER_NAME" folder does not exist beside the runtime.';
			}

			validateRequiredPaths(candidate);
			assetsRoot = candidate;
			assetsAvailable = true;
			preparationError = null;
			return true;
		}
		catch (error:Dynamic)
		{
			assetsAvailable = false;
			assetsRoot = null;
			preparationError = describeError(error);
			trace('[originFunkin] Cannot switch to Origin Funkin: $preparationError');
		}
		#end
		return false;
	}

	public static function prepare():Bool
	{
		if (!active || assetsRoot == null)
		{
			preparationError = '$FOLDER_NAME mode was requested without an asset directory.';
			return false;
		}

		try
		{
			novaFlareIntroVideoPath = resolveNovaFlareIntroVideoPath();
			noticeFontName = resolveNoticeFontName();
			validateRequiredPaths(assetsRoot);
			mountedAssetCount = mountAssetLibraries(assetsRoot);
			registeredFontCount = registerExternalFonts();

			haxe.Log.trace = funkin.util.logging.AnsiTrace.trace;
			funkin.util.logging.AnsiTrace.traceBF();

			funkin.modding.PolymodHandler.loadAllMods();

			debugDisplay = new FunkinDebugDisplay(10, 10, 0xFFFFFF);
			final save = funkin.save.Save.load();
			#if mobile
			MobileShaderConverter.setEnabled(save.novaSettings == null
				? true
				: (save.novaSettings.autoShaderConversion ?? true));
			#end
			OriginFunkinConfig.markOriginEntered();

			funkin.Preferences.fancyPreview = false;
			funkin.util.WindowUtil.setVSyncMode(funkin.Preferences.vsyncMode);

			// The original runtime installs FunkinCameraFrontEnd before FlxGame.
			untyped FlxG.cameras = new funkin.graphics.FunkinCameraFrontEnd();

			trace('[originFunkin] Mounted $mountedAssetCount static assets from "$assetsRoot".');
			trace('[originFunkin] Registered $registeredFontCount external fonts.');
			trace('[originFunkin] Official core scripts and external mods enabled from "${getModRoot()}".');
			return true;
		}
		catch (error:Dynamic)
		{
			preparationError = describeError(error);
			trace('[originFunkin] Startup failed: $preparationError');
			return false;
		}
	}

	public static function getWindowTitle():String
	{
		return "NovaFlare Engine";
	}

	public static function reportRuntimeError(details:String):Void
	{
		preparationError = 'FNF $VERSION runtime error:\n$details';
	}

	public static function getModRoot():String
	{
		var root:String = assetsRoot;
		#if sys
		if (root == null || root.length == 0)
		{
			var located:Null<String> = locateAssetsRoot();
			if (located != null) root = located;
		}
		if (root == null || root.length == 0)
		{
			root = Path.join([
				Sys.getCwd(),
				OriginFunkinConfig.CONTAINER_FOLDER_NAME,
				FOLDER_NAME,
				ASSET_FOLDER_NAME
			]);
		}
		#end
		return OriginFunkinConfig.getModRoot(root);
	}

	#if sys
	static function locateAssetsRoot():Null<String>
	{
		#if android
		var executableDirectory:String = Sys.getCwd();
		#elseif desktop
		var executablePath:String = Sys.programPath();
		var executableDirectory:String = executablePath == null || executablePath.length == 0
			? Sys.getCwd()
			: Path.directory(executablePath);

		if (executableDirectory == null || executableDirectory.length == 0)
		{
			executableDirectory = Sys.getCwd();
		}
		#else
		var executableDirectory:String = Sys.getCwd();
		#end

		var preferred:String = FileSystem.fullPath(Path.join([
			executableDirectory,
			OriginFunkinConfig.CONTAINER_FOLDER_NAME,
			FOLDER_NAME,
			ASSET_FOLDER_NAME
		]));
		if (FileSystem.exists(preferred) && FileSystem.isDirectory(preferred)) return preferred;

		var containerLegacy:String = FileSystem.fullPath(Path.join([
			executableDirectory,
			OriginFunkinConfig.CONTAINER_FOLDER_NAME,
			LEGACY_FOLDER_NAME
		]));
		if (FileSystem.exists(containerLegacy) && FileSystem.isDirectory(containerLegacy)) return containerLegacy;

		var legacy:String = FileSystem.fullPath(Path.join([executableDirectory, LEGACY_FOLDER_NAME]));
		if (FileSystem.exists(legacy) && FileSystem.isDirectory(legacy)) return legacy;

		var titledLegacy:String = FileSystem.fullPath(Path.join([executableDirectory, FOLDER_NAME]));
		return FileSystem.exists(titledLegacy) && FileSystem.isDirectory(titledLegacy) ? titledLegacy : null;
	}
	#end

	static function resolveNoticeFontName():Null<String>
	{
		if (!Assets.exists(NOVAFLARE_NOTICE_FONT_ASSET, AssetType.FONT))
		{
			trace('[originFunkin] NovaFlare Chinese notice font is unavailable: $NOVAFLARE_NOTICE_FONT_ASSET');
			return null;
		}

		var font:Font = Assets.getFont(NOVAFLARE_NOTICE_FONT_ASSET);
		if (font == null || font.fontName == null || font.fontName.length == 0)
		{
			return null;
		}
		Font.registerFont(font);
		return font.fontName;
	}

	static function resolveNovaFlareIntroVideoPath():Null<String>
	{
		#if sys
		if (FileSystem.exists(NOVAFLARE_INTRO_ASSET))
		{
			return FileSystem.fullPath(NOVAFLARE_INTRO_ASSET);
		}
		#end

		if (Assets.exists(NOVAFLARE_INTRO_LIBRARY_ASSET))
		{
			return Assets.getPath(NOVAFLARE_INTRO_LIBRARY_ASSET);
		}

		if (Assets.exists(NOVAFLARE_INTRO_ASSET))
		{
			return Assets.getPath(NOVAFLARE_INTRO_ASSET);
		}

		return null;
	}

	static function validateRequiredPaths(root:String):Void
	{
		#if sys
		var requiredPaths:Array<String> = [
			"fonts",
			"preload/data/introText.txt",
			"preload/images/logoBumpin.png",
			"preload/images/logoBumpin.xml",
			"preload/images/menuBG.png",
			"preload/music/freakyMenu/freakyMenu.ogg",
			"preload/data/songs/tutorial/tutorial-metadata.json",
			"shared",
			"shared/images/characters",
			"songs",
			"songs/tutorial/Inst.ogg",
			"tutorial",
			"week1",
			"week2",
			"week3",
			"week4",
			"week5",
			"week6",
			"week7",
			"weekend1"
		];

		var missing:Array<String> = [];
		for (relativePath in requiredPaths)
		{
			var diskPath:String = Path.join([root, relativePath]);
			if (!FileSystem.exists(diskPath))
			{
				missing.push(relativePath);
			}
		}

		if (missing.length > 0)
		{
			throw 'originFunkin required paths are missing.\nMissing: ${missing.join(", ")}';
		}
		#else
		throw "originFunkin runtime mounting is only available on desktop sys targets.";
		#end
	}

	static function mountAssetLibraries(root:String):Int
	{
		var total:Int = 0;
		mountedLibraries = [];

		var defaultManifest:AssetManifest = createManifest("default", root);
		total += appendTree(defaultManifest, root, "preload", "assets");
		total += appendTree(defaultManifest, root, "fonts", "assets/fonts");
		registerManifest("default", defaultManifest);

		for (libraryName in NAMED_LIBRARIES)
		{
			#if sys
			var libraryPath:String = Path.join([root, libraryName]);
			if (!FileSystem.exists(libraryPath) || !FileSystem.isDirectory(libraryPath))
			{
				continue;
			}
			#end

			var manifest:AssetManifest = createManifest(libraryName, root);
			var count:Int = appendTree(manifest, root, libraryName, 'assets/$libraryName');
			if (count > 0)
			{
				registerManifest(libraryName, manifest);
				total += count;
			}
		}

		// Libraries owned by NovaFlare/CNE are not part of FNF 0.8.7's asset
		// layout. Leaving any of them registered makes Polymod reject the
		// official asset-library map before it can parse core or mod scripts.
		for (foreignLibrary in ["week_assets", "codename_fallback", "codename_mobile"])
		{
			if (Assets.getLibrary(foreignLibrary) != null)
			{
				Assets.unloadLibrary(foreignLibrary);
			}
		}

		if (!Assets.exists("assets/data/introText.txt", AssetType.TEXT)
			|| !Assets.exists("assets/images/logoBumpin.png", AssetType.IMAGE))
		{
			throw "The originFunkin default asset library could not be mounted.";
		}

		return total;
	}

	static function registerExternalFonts():Int
	{
		var registered:Int = 0;
		for (fontId in Assets.list(AssetType.FONT))
		{
			if (!StringTools.startsWith(fontId, "assets/fonts/"))
			{
				continue;
			}

			var font:Font = Assets.getFont(fontId);
			if (font == null || font.fontName == null || font.fontName.length == 0)
			{
				trace('[originFunkin] Unable to register font "$fontId".');
				continue;
			}

			Font.registerFont(font);
			registered++;
		}
		return registered;
	}

	static function createManifest(name:String, root:String):AssetManifest
	{
		var manifest:AssetManifest = new AssetManifest();
		manifest.name = name;
		manifest.rootPath = normalizePath(root);
		manifest.version = 2;
		return manifest;
	}

	static function registerManifest(name:String, manifest:AssetManifest):Void
	{
		var library:AssetLibrary = cast AssetLibrary.fromManifest(manifest);
		if (library == null)
		{
			throw 'Could not create the originFunkin asset library "$name".';
		}

		Assets.registerLibrary(name, library);
		mountedLibraries.push(name);
	}

	static function appendTree(manifest:AssetManifest, root:String, physicalRoot:String, idRoot:String):Int
	{
		#if sys
		var diskRoot:String = Path.join([root, physicalRoot]);
		if (!FileSystem.exists(diskRoot) || !FileSystem.isDirectory(diskRoot))
		{
			return 0;
		}

		return appendDirectory(manifest, diskRoot, physicalRoot, idRoot, "");
		#else
		return 0;
		#end
	}

	#if sys
	static function appendDirectory(manifest:AssetManifest, diskRoot:String, physicalRoot:String, idRoot:String, relativeDirectory:String):Int
	{
		var currentDirectory:String = relativeDirectory.length == 0
			? diskRoot
			: Path.join([diskRoot, relativeDirectory]);
		var entries:Array<String> = FileSystem.readDirectory(currentDirectory);
		entries.sort(Reflect.compare);

		var count:Int = 0;
		for (entry in entries)
		{
			var relativePath:String = relativeDirectory.length == 0 ? entry : '$relativeDirectory/$entry';
			var diskPath:String = Path.join([diskRoot, relativePath]);

			if (FileSystem.isDirectory(diskPath))
			{
				count += appendDirectory(manifest, diskRoot, physicalRoot, idRoot, relativePath);
				continue;
			}

			var normalizedRelative:String = normalizePath(relativePath);
			var manifestPath:String = normalizePath('$physicalRoot/$normalizedRelative');
			var assetId:String = normalizePath('$idRoot/$normalizedRelative');
			var fileSize:Int = FileSystem.stat(diskPath).size;

			manifest.assets.push({
				path: manifestPath,
				id: assetId,
				type: getAssetType(assetId),
				preload: false,
				size: fileSize
			});
			count++;
		}

		return count;
	}
	#end

	static function getAssetType(assetId:String):AssetType
	{
		var lowerId:String = assetId.toLowerCase();
		return switch (Path.extension(lowerId))
		{
			case "png" | "jpg" | "jpeg" | "gif" | "webp": AssetType.IMAGE;
			case "ttf" | "otf": AssetType.FONT;
			case "ogg" | "mp3" | "wav" | "flac":
				lowerId.indexOf("/music/") != -1 || lowerId.indexOf("assets/songs/") != -1
					? AssetType.MUSIC
					: AssetType.SOUND;
			case "txt" | "xml" | "json" | "frag" | "vert" | "srt" | "fnt" | "csv" | "hxc" | "hx" | "hscript" | "lua": AssetType.TEXT;
			default: AssetType.BINARY;
		}
	}

	static inline function normalizePath(path:String):String
	{
		return StringTools.replace(path, "\\", "/");
	}

	static function describeError(error:Dynamic):String
	{
		if (Std.isOfType(error, Exception))
		{
			return (cast error : Exception).message;
		}
		return Std.string(error);
	}
}
