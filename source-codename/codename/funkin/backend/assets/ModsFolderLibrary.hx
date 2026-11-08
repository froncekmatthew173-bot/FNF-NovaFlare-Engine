package codename.funkin.backend.assets;

import openfl.utils.AssetLibrary;
import lime.media.AudioBuffer;
import lime.graphics.Image;
import lime.text.Font;
import lime.utils.Bytes;

#if MOD_SUPPORT
import sys.FileStat;
import sys.FileSystem;
#end

using StringTools;

class ModsFolderLibrary extends AssetLibrary implements IModsAssetLibrary {
	public var basePath:String;
	public var modName:String;
	public var libName:String;
	//public var useImageCache:Bool = true;
	public var prefix = 'assets/';

	public function new(basePath:String, libName:String, ?modName:String) {
		this.basePath = basePath;
		this.libName = libName;
		this.prefix = 'assets/$libName/';
		this.modName = modName == null ? libName : modName;
		super();
	}

	function toString():String {
		return '(ModsFolderLibrary: $modName)';
	}

	#if MOD_SUPPORT
	private static inline var LOOKUP_CACHE_TTL:Float = 6;
	private var editedTimes:Map<String, Float> = [];
	private var directoryEntryCache:Map<String, Map<String, String>> = [];
	private var directoryEntryTimes:Map<String, Float> = [];
	private var fileStatCache:Map<String, FileStat> = [];
	private var fileStatTimes:Map<String, Float> = [];
	public var _parsedAsset:String = null;

	public function invalidateLookupCaches():Void {
		directoryEntryCache.clear();
		directoryEntryTimes.clear();
		fileStatCache.clear();
		fileStatTimes.clear();
	}

	public function getEditedTime(asset:String):Null<Float> {
		return editedTimes[asset];
	}

	public override function getAudioBuffer(id:String):AudioBuffer {
		var path = prepareAsset(id);
		if (path == null) return null;
		var e = AudioBuffer.fromFile(path);
		// LimeAssets.cache.audio.set('$libName:$id', e);
		return e;
	}

	public override function getBytes(id:String):Bytes {
		var path = prepareAsset(id);
		if (path == null) return null;
		var e = Bytes.fromFile(path);
		return e;
	}

	public override function getFont(id:String):Font {
		var path = prepareAsset(id);
		if (path == null) return null;
		return ModsFolder.registerFont(Font.fromFile(path));
	}

	public override function getImage(id:String):Image {
		var path = prepareAsset(id);
		if (path == null) return null;

		var e = Image.fromFile(path);
		return e;
	}

	public override function getPath(id:String):String {
		if (!__parseAsset(id)) return null;
		return getAssetPath();
	}

	public inline function getFolders(folder:String):Array<String>
		return __getFiles(folder, true);

	public inline function getFiles(folder:String):Array<String>
		return __getFiles(folder, false);

	public function __getFiles(folder:String, folders:Bool = false) {
		if (!folder.endsWith("/")) folder += "/";
		if (!__parseAsset(folder)) return [];
		var path = getAssetPath();
		try {
			var result:Array<String> = [];
			for(e in FileSystem.readDirectory(path))
				if (FileSystem.isDirectory('$path$e') == folders)
					result.push(e);
			return result;
		} catch(e) {
			// woops!!
		}
		return [];
	}

	public override function exists(asset:String, type:String):Bool {
		if(!__parseAsset(asset)) return false;
		return resolveParsedAsset();
	}

	private function getDirectoryEntries(directory:String):Map<String, String> {
		var directoryKey = directory.toLowerCase();
		var now = haxe.Timer.stamp();
		if (directoryEntryTimes.exists(directoryKey)
			&& now < directoryEntryTimes.get(directoryKey) + LOOKUP_CACHE_TTL) {
			var cached = directoryEntryCache.get(directoryKey);
			if (cached != null) return cached;
		}

		var entries:Map<String, String> = [];
		var path = directory.length == 0 ? basePath : '$basePath/$directory';
		try {
			for (entry in FileSystem.readDirectory(path))
				entries.set(entry.toLowerCase(), entry);
		} catch (_) {}

		directoryEntryCache.set(directoryKey, entries);
		directoryEntryTimes.set(directoryKey, now);
		return entries;
	}

	private function resolveParsedAsset():Bool {
		#if windows
		return FileSystem.exists(getAssetPath());
		#else
		var normalized = _parsedAsset.replace('\\', '/');
		while (normalized.endsWith('/')) normalized = normalized.substr(0, normalized.length - 1);
		if (normalized.length == 0) return FileSystem.exists(basePath);

		var split = normalized.lastIndexOf('/');
		var directory = split < 0 ? '' : normalized.substr(0, split);
		var fileName = split < 0 ? normalized : normalized.substr(split + 1);
		var actualName = getDirectoryEntries(directory).get(fileName.toLowerCase());
		if (actualName == null) return false;

		_parsedAsset = directory.length == 0 ? actualName : '$directory/$actualName';
		return true;
		#end
	}

	private function getParsedAssetStat():Null<FileStat> {
		var key = _parsedAsset.toLowerCase();
		var now = haxe.Timer.stamp();
		if (fileStatTimes.exists(key) && now < fileStatTimes.get(key) + LOOKUP_CACHE_TTL)
			return fileStatCache.get(key);

		var stat:Null<FileStat> = null;
		try stat = FileSystem.stat(getAssetPath()) catch (_) {}
		if (stat == null) fileStatCache.remove(key);
		else fileStatCache.set(key, stat);
		fileStatTimes.set(key, now);
		return stat;
	}

	private function prepareAsset(id:String):String {
		if (!__parseAsset(id) || !resolveParsedAsset()) return null;
		var stat = getParsedAssetStat();
		if (stat == null) return null;
		editedTimes[id] = stat.mtime.getTime();
		return getAssetPath();
	}

	private function getAssetPath() {
		return '$basePath/$_parsedAsset';
	}

	private function __isCacheValid(cache:Map<String, Dynamic>, asset:String, isLocalCache:Bool = false) {
		if (!editedTimes.exists(asset))
			return false;
		var editedTime = editedTimes[asset];
		if (!__parseAsset(asset) || !resolveParsedAsset()) return false;
		var stat = getParsedAssetStat();
		if (stat == null || editedTime == null || editedTime < stat.mtime.getTime()) {
			// destroy already existing to prevent memory leak!!!
			/*var asset = cache[asset];
			if (asset != null) {
				switch(Type.getClass(asset)) {
					case lime.graphics.Image:
						trace("getting rid of image cause replaced");
						cast(asset, lime.graphics.Image);
				}
			}*/
			return false;
		}

		if (!isLocalCache) asset = '$libName:$asset';

		return cache.exists(asset) && cache[asset] != null;
	}

	private function __parseAsset(asset:String):Bool {
		if (!asset.startsWith(prefix)) return false;
		_parsedAsset = asset.substr(prefix.length);
		if(ModsFolder.useLibFile) {
			var file = new haxe.io.Path(_parsedAsset);
			if(file.file.startsWith("LIB_")) {
				var library = file.file.substr(4);
				if(library != modName) return false;

				_parsedAsset = file.dir + "." + file.ext;
			}
		}
		return true;
	}

	public override function list(type:String):Array<String> {
		var result = [];
		__listAppend(result, '');
		return result;
	}

	function __listAppend(arr:Array<String>, folder:String) {
		for(file in FileSystem.readDirectory('$basePath/$folder')) {
			var fullPath = '$basePath/$folder/$file';
			if (FileSystem.isDirectory(fullPath))
				__listAppend(arr, '$folder$file/');
			else
				arr.push('$prefix$folder$file');
		}
	}
	#end

	// Backwards compat

	@:noCompletion public var folderPath(get, set):String;
	@:noCompletion private inline function get_folderPath():String {
		return basePath;
	}
	@:noCompletion private inline function set_folderPath(value:String):String {
		return basePath = value;
	}
}
