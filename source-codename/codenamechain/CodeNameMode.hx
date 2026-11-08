package codenamechain;

import haxe.Exception;
import haxe.io.Path;
import openfl.utils.Assets;

#if sys
import sys.FileSystem;
#end

/**
 * Locates and prepares the external Codename runtime without changing the
 * working directory used by NovaFlare or the V-Slice chain.
 */
class CodeNameMode
{
	public static inline final FOLDER_NAME:String = "CodeName";
	public static inline final ASSET_FOLDER_NAME:String = "assets";
	public static inline final MOD_FOLDER_NAME:String = "mods-codename";

	public static var active(default, null):Bool = false;
	public static var assetsAvailable(default, null):Bool = false;
	public static var root(default, null):Null<String>;
	public static var assetsRoot(default, null):Null<String>;
	public static var preparationError(default, null):Null<String>;
	public static var novaFlareIntroVideoPath(default, null):Null<String>;

	static inline final NOVAFLARE_INTRO_ASSET:String = "assets/videos/menuExtend/titleIntro.mp4";
	static inline final NOVAFLARE_INTRO_LIBRARY_ASSET:String = "videos:assets/videos/menuExtend/titleIntro.mp4";

	public static function detect():Bool
	{
		active = false;
		#if CODENAME_ENGINE_COMPAT
		flixel.FlxCamera.cneShaderSizingEnabled = false;
		#end
		assetsAvailable = false;
		root = null;
		assetsRoot = null;
		preparationError = null;

		#if (sys && CODENAME_ENGINE_COMPAT)
		originfunkin.OriginFunkinConfig.load();
		if (!originfunkin.OriginFunkinConfig.shouldStartCodeName()) return false;

		try
		{
			var candidate:Null<String> = locateRoot();
			if (candidate == null) throw 'The "$FOLDER_NAME" folder does not exist inside the Funkin runtime folder.';
			validateLayout(candidate);
			setResolvedRoot(candidate);
			active = true;
			flixel.FlxCamera.cneShaderSizingEnabled = true;
		}
		catch (error:Dynamic)
		{
			preparationError = describeError(error);
			trace('[CodeName] $preparationError Falling back to NF.');
		}
		#end

		return active;
	}

	public static function canEnter():Bool
	{
		#if (sys && CODENAME_ENGINE_COMPAT)
		try
		{
			var candidate:Null<String> = locateRoot();
			if (candidate == null) throw 'The "$FOLDER_NAME" folder does not exist inside the Funkin runtime folder.';
			validateLayout(candidate);
			setResolvedRoot(candidate);
			preparationError = null;
			return true;
		}
		catch (error:Dynamic)
		{
			assetsAvailable = false;
			root = null;
			assetsRoot = null;
			preparationError = describeError(error);
			trace('[CodeName] Cannot switch to Codename Engine: $preparationError');
		}
		#end
		return false;
	}

	public static function prepare():Bool
	{
		#if (sys && CODENAME_ENGINE_COMPAT)
		if (!active || root == null || assetsRoot == null)
		{
			preparationError = "Codename mode was requested without a valid external asset directory.";
			return false;
		}

		try
		{
			validateLayout(root);
			var modRoot:String = getModRoot();
			var addonRoot:String = Path.join([root, "addons"]);
			ensureDirectory(modRoot);
			ensureDirectory(addonRoot);

			codename.funkin.backend.assets.ModsFolder.modsPath = Path.addTrailingSlash(modRoot);
			codename.funkin.backend.assets.ModsFolder.addonsPath = Path.addTrailingSlash(addonRoot);
			novaFlareIntroVideoPath = resolveNovaFlareIntroVideoPath();
			trace('[CodeName] External assets: "$assetsRoot".');
			trace('[CodeName] Mods: "$modRoot".');
			return true;
		}
		catch (error:Dynamic)
		{
			preparationError = describeError(error);
			trace('[CodeName] Startup failed: $preparationError');
		}
		#end
		return false;
	}

	public static function getModRoot():String
	{
		return Path.join([root, MOD_FOLDER_NAME]);
	}

	public static function getTempRoot():String
	{
		return root == null ? ".temp" : Path.join([root, ".temp"]);
	}

	#if sys
	static function locateRoot():Null<String>
	{
		#if mobile
		var runtimeDirectory:String = Sys.getCwd();
		#else
		var executablePath:String = Sys.programPath();
		var runtimeDirectory:String = executablePath == null || executablePath.length == 0
			? Sys.getCwd()
			: Path.directory(executablePath);
		if (runtimeDirectory == null || runtimeDirectory.length == 0) runtimeDirectory = Sys.getCwd();
		#end

		var preferred:String = FileSystem.fullPath(Path.join([
			runtimeDirectory,
			originfunkin.OriginFunkinConfig.CONTAINER_FOLDER_NAME,
			FOLDER_NAME
		]));
		if (FileSystem.exists(preferred) && FileSystem.isDirectory(preferred)) return preferred;

		var legacy:String = FileSystem.fullPath(Path.join([runtimeDirectory, FOLDER_NAME]));
		return FileSystem.exists(legacy) && FileSystem.isDirectory(legacy) ? legacy : null;
	}

	static function validateLayout(candidateRoot:String):Void
	{
		var candidateAssets:String = Path.join([candidateRoot, ASSET_FOLDER_NAME]);
		var required:Array<String> = [
			"data/titlescreen/titlescreen.xml",
			"data/titlescreen/introText.txt",
			"data/weeks/weeks.txt",
			"fonts/vcr.ttf",
			"images/menus/menuBG.png",
			"music/freakyMenu.ogg"
		];

		var missing:Array<String> = [];
		for (relativePath in required)
		{
			if (!FileSystem.exists(Path.join([candidateAssets, relativePath]))) missing.push('assets/$relativePath');
		}

		if (missing.length > 0)
		{
			throw 'CodeName assets are incomplete. Missing: ${missing.join(", ")}';
		}
	}

	static function setResolvedRoot(candidate:String):Void
	{
		root = candidate;
		assetsRoot = FileSystem.fullPath(Path.join([candidate, ASSET_FOLDER_NAME]));
		assetsAvailable = true;
	}

	static function ensureDirectory(path:String):Void
	{
		if (!FileSystem.exists(path)) FileSystem.createDirectory(path);
	}
	#end

	static function resolveNovaFlareIntroVideoPath():Null<String>
	{
		#if sys
		if (FileSystem.exists(NOVAFLARE_INTRO_ASSET)) return FileSystem.fullPath(NOVAFLARE_INTRO_ASSET);
		#end
		if (Assets.exists(NOVAFLARE_INTRO_LIBRARY_ASSET)) return Assets.getPath(NOVAFLARE_INTRO_LIBRARY_ASSET);
		if (Assets.exists(NOVAFLARE_INTRO_ASSET)) return Assets.getPath(NOVAFLARE_INTRO_ASSET);
		return null;
	}

	static function describeError(error:Dynamic):String
	{
		if (Std.isOfType(error, Exception)) return cast(error, Exception).message;
		return Std.string(error);
	}
}
