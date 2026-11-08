package codename.funkin.backend.system.macros;

#if macro
import haxe.macro.*;
import haxe.macro.Expr;

/**
 * Macros containing additional help functions to expand HScript capabilities.
 */
class Macros {
	static final LIBRARY_IGNORE = [
		"flixel.system.macros.*",
		"flixel.addons.editors.spine.*",
		"flixel.addons.nape.*",
		"flixel.addons.tile.FlxRayCastTilemap",
		"foxlite.macro.*"
	];

	static final CODENAME_IGNORE = [];

	static final SCRIPT_GENERIC_METHODS:Map<String, Array<String>> = [
		"flixel.math.FlxRandom" => ["getObject", "shuffle", "shuffleArray"],
		"flixel.util.FlxArrayUtil" => ["fastSplice", "swapAndPop", "flatten2DArray"],
		"flixel.group.FlxTypedSpriteGroup" => ["transformChildren", "multiTransformChildren"],
		"flixel.system.frontEnds.InputFrontEnd" => ["add", "addInput", "addUniqueType", "remove", "replace"],
		"flixel.system.frontEnds.PluginFrontEnd" => ["add"]
	];

	static final CODENAME_PACKAGES = [
		"codename.funkin.backend",
		"codename.funkin.editors",
		"codename.funkin.game",
		"codename.funkin.menus",
		"codename.funkin.options",
		"codename.funkin.savedata"
	];

	public static function addAdditionalClasses() {
		for(inc in [
			// FLIXEL
			"flixel.util", "flixel.ui", "flixel.tweens", "flixel.tile", "flixel.text",
			"flixel.system", "flixel.sound", "flixel.path", "flixel.math", "flixel.input",
			"flixel.group", "flixel.graphics", "flixel.effects", "flixel.animation",
			// FLIXEL ADDONS
			"flixel.addons.api", "flixel.addons.display", "flixel.addons.effects", "flixel.addons.ui",
			"flixel.addons.plugin", "flixel.addons.text", "flixel.addons.tile", "flixel.addons.transition",
			"flixel.addons.util",
			// MOBILE
			#if mobile "mobile", #end
			#if android "android", #end
			// OPENFL
			"openfl.system", "openfl.utils",
			// OTHER LIBRARIES & STUFF
			#if THREE_D_SUPPORT
			#if foxlite
			"foxlite",
			"foxlite.animation", "foxlite.color", "foxlite.culling",
			"foxlite.environment", "foxlite.extras", "foxlite.flixel",
			"foxlite.funkin", "foxlite.group", "foxlite.instancing",
			"foxlite.lights", "foxlite.loaders", "foxlite.material",
			"foxlite.math", "foxlite.mesh", "foxlite.physics",
			"foxlite.polyfill", "foxlite.post", "foxlite.renderer",
			"foxlite.skin", "foxlite.sky", "foxlite.stencil",
			"foxlite.system", "foxlite.texture",
			#end
			#end
			#if VIDEO_CUTSCENES "hxvlc.flixel", "hxvlc.openfl", #end
			#if NAPE_ENABLED "nape", "flixel.addons.nape", #end
			#if IMGUI_ENABLED "lime.tools.imgui", #end
			// BASE HAXE
			"DateTools", "EReg", "Lambda", "StringBuf", "haxe.crypto", "haxe.display", "haxe.exceptions", "haxe.extern", "scripting", "animate"
		])
			Compiler.include(inc, true, LIBRARY_IGNORE);

		var isHl = Context.defined("hl");

		var compathx4 = [
			"sys.db.Sqlite",
			"sys.db.Mysql",
			"sys.db.Connection",
			"sys.db.ResultSet",
			"haxe.remoting.Proxy",
		];

		if(Context.defined("sys")) {
			for(inc in ["sys", "openfl.net", "codename.funkin.backend.system.net"]) {
				if(!isHl) Compiler.include(inc, compathx4);
				else {

					// TODO: Hashlink
					//Compiler.include(inc, compathx4.concat(["sys.net.UdpSocket", "openfl.net.DatagramSocket"]); // fixes FATAL ERROR : Failed to load function std@socket_set_broadcast
				}
			}
		}

		Compiler.include("codename.funkin", [#if !UPDATE_CHECKING 'codename.funkin.backend.system.updating' #end]);
	}

	public static function includeCodeNameClasses() {
		for(inc in CODENAME_PACKAGES)
			Compiler.include(inc, true, CODENAME_IGNORE);

		Compiler.include("codename.funkin", false, CODENAME_IGNORE);

		if (Context.defined("mobile"))
			Compiler.include("codename.mobile");
	}

	public static function initMacros() {
		if (Context.defined("hl")) {
			for (c in ["lime", "std", "Math", ""]) Compiler.addGlobalMetadata(c, "@:build(codename.funkin.backend.system.macros.HashLinkFixer.build())");
		}

		final macroPath = 'codename.funkin.backend.system.macros.Macros';
		Compiler.addMetadata('@:build($macroPath.buildLimeAssetLibrary())', 'lime.utils.AssetLibrary');
		Compiler.addMetadata('@:build($macroPath.buildLimeApplication())', 'lime.app.Application');
		Compiler.addMetadata('@:build($macroPath.buildLimeWindow())', 'lime.ui.Window');
		Compiler.addMetadata('@:build($macroPath.buildOpenflAssets())', 'openfl.utils.Assets');
		// Use a package filter for secondary types such as FlxTypedSpriteGroup.
		for (path in ["flixel.math.FlxRandom", "flixel.util.FlxArrayUtil", "flixel.group", "flixel.system.frontEnds"])
			Compiler.addGlobalMetadata(path, '@:build($macroPath.buildScriptGenericMethods())');

		//Adds Compat for #if hscript blocks when you have hscript improved
		if (Context.defined("hscript_improved") && !Context.defined("hscript")) {
			Compiler.define('hscript');
		}
	}

	public static function buildScriptGenericMethods():Array<Field> {
		final fields = Context.getBuildFields();
		final classRef = Context.getLocalClass();
		if (classRef == null) return fields;
		final localClass = classRef.get();
		final methods = SCRIPT_GENERIC_METHODS.get(localClass.pack.concat([localClass.name]).join("."));
		if (methods == null) return fields;
		// @:generic replaces the original method with type-specific names on
		// native targets. HScript calls these methods by their original names.
		// Preserve their bodies and type parameters; only disable specialization
		// for the listed script-facing methods. Class-level generics are untouched.
		for (field in fields) {
			if (!methods.contains(field.name)) continue;
			field.meta = field.meta == null ? [] : field.meta.filter(meta -> meta.name != ":generic");
			if (!Lambda.exists(field.meta, meta -> meta.name == ":keep"))
				field.meta.push({name: ":keep", params: [], pos: field.pos});
		}
		return fields;
	}

	public static function buildLimeAssetLibrary():Array<Field> {
		final fields:Array<Field> = Context.getBuildFields(), pos:Position = Context.currentPos();

		fields.push({name: 'tag', access: [APublic], pos: pos, kind: FVar(macro :codename.funkin.backend.assets.AssetSource)});
		fields.push({name: 'isCompressed', access: [APublic], pos: pos, kind: FVar(macro :Bool, macro false)});

		return fields;
	}

	public static function buildLimeApplication():Array<Field> {
		final fields:Array<Field> = Context.getBuildFields(), pos:Position = Context.currentPos();
		for (f in fields) switch (f.kind) {
			case FFun(func): switch (f.name) {
				case "exec": switch (func.expr.expr) {
					case EBlock(exprs): exprs.insert(1, macro codename.funkin.backend.system.Main.preInit());
					default:
				}
			}
			default:
		}

		return fields;
	}

	public static function buildLimeWindow():Array<Field> {
		final fields:Array<Field> = Context.getBuildFields(), pos:Position = Context.currentPos();
		if (!Context.defined("DARK_MODE_WINDOW")) return fields;

		for (f in fields) switch (f.kind) {
			case FFun(func): switch (f.name) {
				case "new": switch (func.expr.expr) {
					case EBlock(exprs): exprs.push(macro codename.funkin.backend.utils.NativeAPI.setDarkMode(title, true));
					default:
				}
			}
			default:
		}

		return fields;
	}

	public static function buildOpenflAssets():Array<Field> {
		final fields:Array<Field> = Context.getBuildFields(), pos:Position = Context.currentPos();
		for (f in fields) switch (f.name) {
			case "allowHardwareTextures": fields.remove(f);
			default:
		}

		fields.push({name: 'allowHardwareTextures', access: [APublic, AStatic], pos: pos, kind: FProp("get", "set", macro :Bool)});
		fields.push({name: '__allowHardwareTextures', access: [APrivate, AStatic], pos: pos, kind: FVar(macro :Null<Bool>)});

		fields.push({name: "get_allowHardwareTextures", access: [APublic, AStatic, AInline], pos: pos, kind: FFun({ret: macro :Bool, args: [], expr: macro {
			return __allowHardwareTextures != null ? __allowHardwareTextures : !codename.funkin.backend.system.Main.forceGPUOnlyBitmapsOff && codename.funkin.options.Options.gpuOnlyBitmaps;
		}})});
		fields.push({name: "set_allowHardwareTextures", access: [APublic, AStatic, AInline], pos: pos, kind: FFun({ret: macro :Bool, args: [{name: "value", type: macro :Bool}], expr: macro {
			__allowHardwareTextures = value;
			return get_allowHardwareTextures();
		}})});

		return fields;
	}
}
#end
