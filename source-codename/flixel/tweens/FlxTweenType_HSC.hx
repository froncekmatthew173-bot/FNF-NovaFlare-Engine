package flixel.tweens;

#if CODENAME_ENGINE_COMPAT
import flixel.tweens.FlxTween.FlxTweenType;

/** Runtime reflection surface for the FlxTweenType abstract used by HScript. */
class FlxTweenType_HSC
{
	public static var PERSIST(get, never):FlxTweenType;
	public static var LOOPING(get, never):FlxTweenType;
	public static var PINGPONG(get, never):FlxTweenType;
	public static var ONESHOT(get, never):FlxTweenType;
	public static var BACKWARD(get, never):FlxTweenType;

	private static inline function get_PERSIST():FlxTweenType
		return FlxTweenType.PERSIST;
	private static inline function get_LOOPING():FlxTweenType
		return FlxTweenType.LOOPING;
	private static inline function get_PINGPONG():FlxTweenType
		return FlxTweenType.PINGPONG;
	private static inline function get_ONESHOT():FlxTweenType
		return FlxTweenType.ONESHOT;
	private static inline function get_BACKWARD():FlxTweenType
		return FlxTweenType.BACKWARD;
}
#end
