package mobile.funkin.backend.utils;

#if TOUCH_CONTROLS
import flixel.FlxCamera;
import flixel.FlxG;
import flixel.FlxObject;
import flixel.input.touch.FlxTouch;

/** Stateless helpers for querying the current Flixel touch frame. */
class TouchUtil
{
	public static var pressed(get, never):Bool;
	public static var justPressed(get, never):Bool;
	public static var justReleased(get, never):Bool;
	public static var released(get, never):Bool;
	public static var touch(get, never):FlxTouch;

	public static function overlaps(object:FlxObject, ?camera:FlxCamera):Bool
	{
		if (object == null) return false;
		var targetCamera = camera != null ? camera : object.camera;
		for (pointer in FlxG.touches.list)
			if (pointer != null && pointer.overlaps(object, targetCamera)) return true;
		return false;
	}

	/** Pixel-perfect overlap across every active object camera and every touch. */
	public static function overlapsComplex(object:FlxObject, ?camera:FlxCamera):Bool
	{
		if (object == null) return false;

		if (camera != null) return overlapsPointInCamera(object, camera);
		var targetCameras = object.cameras;
		if (targetCameras == null || targetCameras.length == 0)
			return object.camera != null && overlapsPointInCamera(object, object.camera);
		for (targetCamera in targetCameras)
			if (targetCamera != null && overlapsPointInCamera(object, targetCamera)) return true;
		return false;
	}

	static function overlapsPointInCamera(object:FlxObject, camera:FlxCamera):Bool
	{
		for (pointer in FlxG.touches.list)
		{
			if (pointer == null) continue;
			@:privateAccess
			if (object.overlapsPoint(pointer.getWorldPosition(camera, object._point), true, camera)) return true;
		}
		return false;
	}

	static function anyTouch(matches:FlxTouch->Bool):Bool
	{
		for (pointer in FlxG.touches.list)
			if (pointer != null && matches(pointer)) return true;
		return false;
	}

	@:noCompletion
	private static inline function get_pressed():Bool
		return anyTouch(pointer -> pointer.pressed);

	@:noCompletion
	private static inline function get_justPressed():Bool
		return anyTouch(pointer -> pointer.justPressed);

	@:noCompletion
	private static inline function get_justReleased():Bool
		return anyTouch(pointer -> pointer.justReleased);

	@:noCompletion
	private static inline function get_released():Bool
		return anyTouch(pointer -> pointer.released);

	@:noCompletion
	private static function get_touch():FlxTouch
	{
		for (pointer in FlxG.touches.list)
			if (pointer != null) return pointer;
		return FlxG.touches.getFirst();
	}
}
#end
