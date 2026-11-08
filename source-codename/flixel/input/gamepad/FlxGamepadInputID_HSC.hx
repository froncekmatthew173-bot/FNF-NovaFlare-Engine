package flixel.input.gamepad;

#if CODENAME_ENGINE_COMPAT
/** Runtime reflection surface for the FlxGamepadInputID abstract used by HScript. */
class FlxGamepadInputID_HSC
{
	public static var fromStringMap(get, never):Map<String, FlxGamepadInputID>;
	public static var toStringMap(get, never):Map<FlxGamepadInputID, String>;

	public static var ANY(get, never):FlxGamepadInputID;
	public static var NONE(get, never):FlxGamepadInputID;
	public static var A(get, never):FlxGamepadInputID;
	public static var B(get, never):FlxGamepadInputID;
	public static var X(get, never):FlxGamepadInputID;
	public static var Y(get, never):FlxGamepadInputID;
	public static var LEFT_SHOULDER(get, never):FlxGamepadInputID;
	public static var RIGHT_SHOULDER(get, never):FlxGamepadInputID;
	public static var BACK(get, never):FlxGamepadInputID;
	public static var START(get, never):FlxGamepadInputID;
	public static var LEFT_STICK_CLICK(get, never):FlxGamepadInputID;
	public static var RIGHT_STICK_CLICK(get, never):FlxGamepadInputID;
	public static var GUIDE(get, never):FlxGamepadInputID;
	public static var DPAD_UP(get, never):FlxGamepadInputID;
	public static var DPAD_DOWN(get, never):FlxGamepadInputID;
	public static var DPAD_LEFT(get, never):FlxGamepadInputID;
	public static var DPAD_RIGHT(get, never):FlxGamepadInputID;
	public static var LEFT_TRIGGER_BUTTON(get, never):FlxGamepadInputID;
	public static var RIGHT_TRIGGER_BUTTON(get, never):FlxGamepadInputID;
	public static var LEFT_TRIGGER(get, never):FlxGamepadInputID;
	public static var RIGHT_TRIGGER(get, never):FlxGamepadInputID;
	public static var LEFT_ANALOG_STICK(get, never):FlxGamepadInputID;
	public static var RIGHT_ANALOG_STICK(get, never):FlxGamepadInputID;
	public static var DPAD(get, never):FlxGamepadInputID;
	#if FLX_JOYSTICK_API
	public static var LEFT_TRIGGER_FAKE(get, never):FlxGamepadInputID;
	public static var RIGHT_TRIGGER_FAKE(get, never):FlxGamepadInputID;
	public static var LEFT_STICK_FAKE(get, never):FlxGamepadInputID;
	public static var RIGHT_STICK_FAKE(get, never):FlxGamepadInputID;
	#end
	public static var TILT_PITCH(get, never):FlxGamepadInputID;
	public static var TILT_ROLL(get, never):FlxGamepadInputID;
	public static var POINTER_X(get, never):FlxGamepadInputID;
	public static var POINTER_Y(get, never):FlxGamepadInputID;
	public static var EXTRA_0(get, never):FlxGamepadInputID;
	public static var EXTRA_1(get, never):FlxGamepadInputID;
	public static var EXTRA_2(get, never):FlxGamepadInputID;
	public static var EXTRA_3(get, never):FlxGamepadInputID;
	public static var LEFT_STICK_DIGITAL_UP(get, never):FlxGamepadInputID;
	public static var LEFT_STICK_DIGITAL_RIGHT(get, never):FlxGamepadInputID;
	public static var LEFT_STICK_DIGITAL_DOWN(get, never):FlxGamepadInputID;
	public static var LEFT_STICK_DIGITAL_LEFT(get, never):FlxGamepadInputID;
	public static var RIGHT_STICK_DIGITAL_UP(get, never):FlxGamepadInputID;
	public static var RIGHT_STICK_DIGITAL_RIGHT(get, never):FlxGamepadInputID;
	public static var RIGHT_STICK_DIGITAL_DOWN(get, never):FlxGamepadInputID;
	public static var RIGHT_STICK_DIGITAL_LEFT(get, never):FlxGamepadInputID;
	public static var ACCEPT(get, never):FlxGamepadInputID;
	public static var CANCEL(get, never):FlxGamepadInputID;

	private static inline function get_fromStringMap():Map<String, FlxGamepadInputID>
		return FlxGamepadInputID.fromStringMap;
	private static inline function get_toStringMap():Map<FlxGamepadInputID, String>
		return FlxGamepadInputID.toStringMap;

	private static inline function get_ANY():FlxGamepadInputID return FlxGamepadInputID.ANY;
	private static inline function get_NONE():FlxGamepadInputID return FlxGamepadInputID.NONE;
	private static inline function get_A():FlxGamepadInputID return FlxGamepadInputID.A;
	private static inline function get_B():FlxGamepadInputID return FlxGamepadInputID.B;
	private static inline function get_X():FlxGamepadInputID return FlxGamepadInputID.X;
	private static inline function get_Y():FlxGamepadInputID return FlxGamepadInputID.Y;
	private static inline function get_LEFT_SHOULDER():FlxGamepadInputID return FlxGamepadInputID.LEFT_SHOULDER;
	private static inline function get_RIGHT_SHOULDER():FlxGamepadInputID return FlxGamepadInputID.RIGHT_SHOULDER;
	private static inline function get_BACK():FlxGamepadInputID return FlxGamepadInputID.BACK;
	private static inline function get_START():FlxGamepadInputID return FlxGamepadInputID.START;
	private static inline function get_LEFT_STICK_CLICK():FlxGamepadInputID return FlxGamepadInputID.LEFT_STICK_CLICK;
	private static inline function get_RIGHT_STICK_CLICK():FlxGamepadInputID return FlxGamepadInputID.RIGHT_STICK_CLICK;
	private static inline function get_GUIDE():FlxGamepadInputID return FlxGamepadInputID.GUIDE;
	private static inline function get_DPAD_UP():FlxGamepadInputID return FlxGamepadInputID.DPAD_UP;
	private static inline function get_DPAD_DOWN():FlxGamepadInputID return FlxGamepadInputID.DPAD_DOWN;
	private static inline function get_DPAD_LEFT():FlxGamepadInputID return FlxGamepadInputID.DPAD_LEFT;
	private static inline function get_DPAD_RIGHT():FlxGamepadInputID return FlxGamepadInputID.DPAD_RIGHT;
	private static inline function get_LEFT_TRIGGER_BUTTON():FlxGamepadInputID return FlxGamepadInputID.LEFT_TRIGGER_BUTTON;
	private static inline function get_RIGHT_TRIGGER_BUTTON():FlxGamepadInputID return FlxGamepadInputID.RIGHT_TRIGGER_BUTTON;
	private static inline function get_LEFT_TRIGGER():FlxGamepadInputID return FlxGamepadInputID.LEFT_TRIGGER;
	private static inline function get_RIGHT_TRIGGER():FlxGamepadInputID return FlxGamepadInputID.RIGHT_TRIGGER;
	private static inline function get_LEFT_ANALOG_STICK():FlxGamepadInputID return FlxGamepadInputID.LEFT_ANALOG_STICK;
	private static inline function get_RIGHT_ANALOG_STICK():FlxGamepadInputID return FlxGamepadInputID.RIGHT_ANALOG_STICK;
	private static inline function get_DPAD():FlxGamepadInputID return FlxGamepadInputID.DPAD;
	#if FLX_JOYSTICK_API
	private static inline function get_LEFT_TRIGGER_FAKE():FlxGamepadInputID return FlxGamepadInputID.LEFT_TRIGGER_FAKE;
	private static inline function get_RIGHT_TRIGGER_FAKE():FlxGamepadInputID return FlxGamepadInputID.RIGHT_TRIGGER_FAKE;
	private static inline function get_LEFT_STICK_FAKE():FlxGamepadInputID return FlxGamepadInputID.LEFT_STICK_FAKE;
	private static inline function get_RIGHT_STICK_FAKE():FlxGamepadInputID return FlxGamepadInputID.RIGHT_STICK_FAKE;
	#end
	private static inline function get_TILT_PITCH():FlxGamepadInputID return FlxGamepadInputID.TILT_PITCH;
	private static inline function get_TILT_ROLL():FlxGamepadInputID return FlxGamepadInputID.TILT_ROLL;
	private static inline function get_POINTER_X():FlxGamepadInputID return FlxGamepadInputID.POINTER_X;
	private static inline function get_POINTER_Y():FlxGamepadInputID return FlxGamepadInputID.POINTER_Y;
	private static inline function get_EXTRA_0():FlxGamepadInputID return FlxGamepadInputID.EXTRA_0;
	private static inline function get_EXTRA_1():FlxGamepadInputID return FlxGamepadInputID.EXTRA_1;
	private static inline function get_EXTRA_2():FlxGamepadInputID return FlxGamepadInputID.EXTRA_2;
	private static inline function get_EXTRA_3():FlxGamepadInputID return FlxGamepadInputID.EXTRA_3;
	private static inline function get_LEFT_STICK_DIGITAL_UP():FlxGamepadInputID return FlxGamepadInputID.LEFT_STICK_DIGITAL_UP;
	private static inline function get_LEFT_STICK_DIGITAL_RIGHT():FlxGamepadInputID return FlxGamepadInputID.LEFT_STICK_DIGITAL_RIGHT;
	private static inline function get_LEFT_STICK_DIGITAL_DOWN():FlxGamepadInputID return FlxGamepadInputID.LEFT_STICK_DIGITAL_DOWN;
	private static inline function get_LEFT_STICK_DIGITAL_LEFT():FlxGamepadInputID return FlxGamepadInputID.LEFT_STICK_DIGITAL_LEFT;
	private static inline function get_RIGHT_STICK_DIGITAL_UP():FlxGamepadInputID return FlxGamepadInputID.RIGHT_STICK_DIGITAL_UP;
	private static inline function get_RIGHT_STICK_DIGITAL_RIGHT():FlxGamepadInputID return FlxGamepadInputID.RIGHT_STICK_DIGITAL_RIGHT;
	private static inline function get_RIGHT_STICK_DIGITAL_DOWN():FlxGamepadInputID return FlxGamepadInputID.RIGHT_STICK_DIGITAL_DOWN;
	private static inline function get_RIGHT_STICK_DIGITAL_LEFT():FlxGamepadInputID return FlxGamepadInputID.RIGHT_STICK_DIGITAL_LEFT;
	private static inline function get_ACCEPT():FlxGamepadInputID return FlxGamepadInputID.ACCEPT;
	private static inline function get_CANCEL():FlxGamepadInputID return FlxGamepadInputID.CANCEL;

	public static inline function fromString(value:String):FlxGamepadInputID
		return FlxGamepadInputID.fromString(value);
}
#end
