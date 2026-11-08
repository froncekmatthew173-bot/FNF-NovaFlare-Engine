package codename.funkin.editors.ui;

#if mobile
import flixel.tweens.FlxTween;
import flixel.util.FlxColor;
import codename.funkin.backend.FunkinText;
import haxe.io.Path;
import sys.FileSystem;

/**
 * Touch-friendly file picker for targets where Lime's native FileDialog is
 * unavailable. Browsing is intentionally contained inside the CodeName root.
 */
@:noCustomClass
class MobileFilePickerSubstate extends MusicBeatSubstate
{
	var rootPath:String;
	var currentPath:String;
	var allowedExtensions:Array<String>;
	var onSelect:String->Void;

	var entries:Array<MobileFileEntry> = [];
	var labels:FlxTypedGroup<Alphabet>;
	var title:FunkinText;
	var backLabel:FunkinText;
	var emptyLabel:FunkinText;
	var pickerCamera:FlxCamera;
	var curSelected:Int = 0;
	var previousMouseVisible:Bool;
	var previousParentPersistentUpdate:Bool;
	var previousParentPersistentDraw:Bool;

	public function new(filter:String, onSelect:String->Void)
	{
		super(false);
		this.onSelect = onSelect;
		allowedExtensions = parseExtensions(filter);

		var resolvedRoot = codenamechain.CodeNameMode.root;
		rootPath = FileSystem.fullPath(resolvedRoot == null || resolvedRoot.length == 0 ? Sys.getCwd() : resolvedRoot);
		currentPath = rootPath;
	}

	public override function onSubstateOpen():Void
	{
		super.onSubstateOpen();
		previousParentPersistentUpdate = parent.persistentUpdate;
		previousParentPersistentDraw = parent.persistentDraw;
		parent.persistentUpdate = false;
		parent.persistentDraw = true;
	}

	override function create():Void
	{
		super.create();

		previousMouseVisible = FlxG.mouse.visible;
		FlxG.mouse.visible = !controls.touchC;

		camera = pickerCamera = new FlxCamera();
		pickerCamera.bgColor = 0;
		FlxG.cameras.add(pickerCamera, false);

		var bg = new FlxSprite().makeSolid(FlxG.width, FlxG.height, FlxColor.BLACK);
		bg.alpha = 0;
		bg.scrollFactor.set();
		add(bg);

		title = new FunkinText(16, 12, FlxG.width - 32, "", 24, false);
		title.scrollFactor.set();
		add(title);

		backLabel = new FunkinText(FlxG.width - 216, 12, 200, "[ Back / Cancel ]", 20, false);
		backLabel.alignment = RIGHT;
		backLabel.scrollFactor.set();
		add(backLabel);

		emptyLabel = new FunkinText(16, FlxG.height * 0.5 - 16, FlxG.width - 32,
			"No matching files in this folder", 24, false);
		emptyLabel.alignment = CENTER;
		emptyLabel.scrollFactor.set();
		add(emptyLabel);

		labels = new FlxTypedGroup<Alphabet>();
		add(labels);

		var help = new FunkinText(16, FlxG.height - 42, FlxG.width - 32,
			"A: open/select    B: parent/cancel", 20, false);
		help.alignment = CENTER;
		help.scrollFactor.set();
		add(help);

		refreshDirectory();
		FlxTween.tween(bg, {alpha: 0.92}, 0.15);
	}

	override function update(elapsed:Float):Void
	{
		super.update(elapsed);

		if (entries.length > 0) {
			var change = (controls.UP_P ? -1 : 0) + (controls.DOWN_P ? 1 : 0);
			if (change != 0) changeSelection(change);

			if (controls.ACCEPT) activateSelection();
		}

		if (controls.BACK) navigateBack();

		// When the virtual pad is disabled, keep the picker usable with touch-as-
		// mouse, a physical mouse, or a trackpad. Do not process pointer input while
		// the pad is active, otherwise a single touch could trigger both paths.
		if (!controls.touchC) {
			FlxG.mouse.visible = true;
			if (FlxG.mouse.justPressedRight || (FlxG.mouse.justPressed && FlxG.mouse.overlaps(backLabel, pickerCamera))) {
				navigateBack();
			} else if (FlxG.mouse.justPressed) {
				for (index => label in labels.members) if (label != null && FlxG.mouse.overlaps(label, pickerCamera)) {
					curSelected = index;
					updateSelection();
					activateSelection();
					break;
				}
			}
		}
	}

	function activateSelection():Void
	{
		if (entries.length == 0) return;
		var entry = entries[curSelected];
		if (entry.isDirectory) {
			currentPath = entry.path;
			refreshDirectory();
		} else {
			var callback = onSelect;
			close();
			if (callback != null) callback(entry.path);
		}
	}

	function navigateBack():Void
	{
		if (samePath(currentPath, rootPath)) close();
		else {
			var parentPath = FileSystem.fullPath(Path.directory(currentPath));
			currentPath = isInsideRoot(parentPath) ? parentPath : rootPath;
			refreshDirectory();
		}
	}

	function refreshDirectory():Void
	{
		for (label in labels.members) if (label != null) label.destroy();
		labels.clear();
		entries.resize(0);

		try {
			var directories:Array<MobileFileEntry> = [];
			var files:Array<MobileFileEntry> = [];
			for (name in FileSystem.readDirectory(currentPath)) {
				if (name == ".temp") continue;
				var candidate = FileSystem.fullPath(Path.join([currentPath, name]));
				if (!isInsideRoot(candidate)) continue;
				if (FileSystem.isDirectory(candidate))
					directories.push({name: name, path: candidate, isDirectory: true});
				else if (matchesFilter(name))
					files.push({name: name, path: candidate, isDirectory: false});
			}

			directories.sort(sortEntries);
			files.sort(sortEntries);
			entries = directories.concat(files);
		} catch (error:Dynamic) {
			Logs.error('Unable to browse "$currentPath": ${Std.string(error)}');
		}

		for (index => entry in entries) {
			var label = new Alphabet(0, 0, (entry.isDirectory ? "> " : "") + entry.name, "bold");
			label.isMenuItem = true;
			label.targetY = index;
			label.scrollFactor.set();
			labels.add(label);
		}

		curSelected = 0;
		emptyLabel.visible = entries.length == 0;
		var relative = samePath(currentPath, rootPath) ? "." : currentPath.substr(Path.addTrailingSlash(rootPath).length);
		title.text = 'Select a file - CodeName/$relative';
		updateSelection();
	}

	function changeSelection(change:Int):Void
	{
		curSelected = FlxMath.wrap(curSelected + change, 0, entries.length - 1);
		CoolUtil.playMenuSFX(SCROLL, 0.7);
		updateSelection();
	}

	function updateSelection():Void
	{
		for (index => label in labels.members) if (label != null) {
			label.targetY = index - curSelected;
			label.alpha = index == curSelected ? 1 : 0.6;
		}
	}

	inline function matchesFilter(name:String):Bool
	{
		var extension = Path.extension(name);
		return allowedExtensions.length == 0
			|| (extension != null && allowedExtensions.contains(extension.toLowerCase()));
	}

	function isInsideRoot(path:String):Bool
	{
		var normalizedRoot = Path.addTrailingSlash(pathKey(rootPath));
		var normalizedPath = pathKey(path);
		return normalizedPath == pathKey(rootPath) || normalizedPath.startsWith(normalizedRoot);
	}

	inline function samePath(a:String, b:String):Bool
		return pathKey(a) == pathKey(b);

	static inline function pathKey(path:String):String
		return #if windows Path.normalize(path).toLowerCase() #else Path.normalize(path) #end;

	static function parseExtensions(filter:String):Array<String>
	{
		if (filter == null) return [];
		var cleaned = filter.toLowerCase().replace("*", "").replace(".", "");
		for (separator in [";", ",", "|"]) cleaned = cleaned.replace(separator, " ");
		return [for (part in cleaned.split(" ")) if (part.trim().length > 0) part.trim()];
	}

	static inline function sortEntries(a:MobileFileEntry, b:MobileFileEntry):Int
		return Reflect.compare(a.name.toLowerCase(), b.name.toLowerCase());

	override function destroy():Void
	{
		FlxG.mouse.visible = previousMouseVisible;
		if (parent != null) {
			parent.persistentUpdate = previousParentPersistentUpdate;
			parent.persistentDraw = previousParentPersistentDraw;
		}
		if (pickerCamera != null && FlxG.cameras.list.contains(pickerCamera))
			FlxG.cameras.remove(pickerCamera);
		pickerCamera = null;
		onSelect = null;
		super.destroy();
	}
}

private typedef MobileFileEntry = {
	var name:String;
	var path:String;
	var isDirectory:Bool;
}
#end
