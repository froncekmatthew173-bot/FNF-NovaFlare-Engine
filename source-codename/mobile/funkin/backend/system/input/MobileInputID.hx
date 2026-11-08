package mobile.funkin.backend.system.input;

#if TOUCH_CONTROLS
using StringTools;

/**
 * Stable identifiers shared by touch-pad layouts, gameplay hitboxes and
 * Codename's control bridge. Numeric values are part of the scripting API.
 */
@:runtimeValue
enum abstract MobileInputID(Int) from Int to Int
{
	var ANY = -2;
	var NONE = -1;

	var A = 0;
	var B = 1;
	var C = 2;
	var D = 3;
	var E = 4;
	var F = 5;
	var G = 6;
	var H = 7;
	var I = 8;
	var J = 9;
	var K = 10;
	var L = 11;
	var M = 12;
	var N = 13;
	var O = 14;
	var P = 15;
	var Q = 16;
	var R = 17;
	var S = 18;
	var T = 19;
	var U = 20;
	var V = 21;
	var W = 22;
	var X = 23;
	var Y = 24;
	var Z = 25;

	var UP = 26;
	var UP2 = 27;
	var DOWN = 28;
	var DOWN2 = 29;
	var LEFT = 30;
	var LEFT2 = 31;
	var RIGHT = 32;
	var RIGHT2 = 33;

	var HITBOX_UP = 34;
	var HITBOX_DOWN = 35;
	var HITBOX_LEFT = 36;
	var HITBOX_RIGHT = 37;
	var EXTRA_1 = 38;
	var EXTRA_2 = 39;

	private static final ID_NAMES:Array<String> = [
		"ANY", "NONE",
		"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
		"N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
		"UP", "UP2", "DOWN", "DOWN2", "LEFT", "LEFT2", "RIGHT", "RIGHT2",
		"HITBOX_UP", "HITBOX_DOWN", "HITBOX_LEFT", "HITBOX_RIGHT", "EXTRA_1", "EXTRA_2"
	];

	private static final ID_VALUES:Array<Int> = [
		-2, -1,
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
		13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
		26, 27, 28, 29, 30, 31, 32, 33,
		34, 35, 36, 37, 38, 39
	];

	/** Compatibility maps retained for scripts and existing layout loaders. */
	public static var fromStringMap(default, null):Map<String, MobileInputID> = buildNameMap();
	public static var toStringMap(default, null):Map<MobileInputID, String> = buildValueMap();

	private static function buildNameMap():Map<String, MobileInputID>
	{
		var result = new Map<String, MobileInputID>();
		for (index in 0...ID_NAMES.length)
			result.set(ID_NAMES[index], cast ID_VALUES[index]);
		return result;
	}

	private static function buildValueMap():Map<MobileInputID, String>
	{
		var result = new Map<MobileInputID, String>();
		for (index in 0...ID_VALUES.length)
			result.set(cast ID_VALUES[index], ID_NAMES[index]);
		return result;
	}

	public static function normalizeName(value:String):String
	{
		if (value == null) return "";
		return value.trim().toUpperCase().replace("-", "_").replace(" ", "_");
	}

	@:from
	public static inline function fromString(value:String):MobileInputID
	{
		var normalized = normalizeName(value);
		return fromStringMap.exists(normalized) ? fromStringMap.get(normalized) : NONE;
	}

	public static inline function fromValue(value:Int):MobileInputID
		return toStringMap.exists(cast value) ? cast value : NONE;

	public static inline function isKnown(value:MobileInputID):Bool
		return toStringMap.exists(value);

	@:to
	public inline function toString():String
	{
		return toStringMap.get(this);
	}
}
#end
