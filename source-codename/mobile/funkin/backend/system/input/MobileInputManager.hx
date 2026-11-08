package mobile.funkin.backend.system.input;

#if TOUCH_CONTROLS
import flixel.FlxG;
import flixel.group.FlxSpriteGroup.FlxTypedSpriteGroup;
import haxe.ds.Map;
import mobile.objects.TouchButton;

/**
 * Owns the ID-to-button bindings for one visible mobile-control layer.
 *
 * More than one button may advertise the same ID. `trackedButtons` remains as
 * a compatibility view containing the first button for each ID, while input
 * queries use the complete binding table.
 */
class MobileInputManager extends FlxTypedSpriteGroup<TouchButton>
{
	public var trackedButtons:Map<MobileInputID, TouchButton> = new Map<MobileInputID, TouchButton>();
	public static var instance:Null<MobileInputManager> = null;
	static var liveManagers:Array<MobileInputManager> = [];

	var bindings:Map<MobileInputID, Array<TouchButton>> = new Map<MobileInputID, Array<TouchButton>>();

	public function new()
	{
		super();
		liveManagers.push(this);
		instance = this;
	}

	override public function destroy():Void
	{
		liveManagers.remove(this);
		if (instance == this)
			instance = liveManagers.length > 0 ? liveManagers[liveManagers.length - 1] : null;
		trackedButtons.clear();
		bindings.clear();
		super.destroy();
	}

	public inline function buttonPressed(button:MobileInputID):Bool
		return checkStatus(button, PRESSED);

	public inline function buttonJustPressed(button:MobileInputID):Bool
		return checkStatus(button, JUST_PRESSED);

	public inline function buttonJustReleased(button:MobileInputID):Bool
		return checkStatus(button, JUST_RELEASED);

	public inline function buttonReleased(button:MobileInputID):Bool
		return checkStatus(button, RELEASED);

	public inline function anyPressed(buttonsArray:Array<MobileInputID>):Bool
		return checkAny(buttonsArray, PRESSED);

	public inline function anyJustPressed(buttonsArray:Array<MobileInputID>):Bool
		return checkAny(buttonsArray, JUST_PRESSED);

	public inline function anyJustReleased(buttonsArray:Array<MobileInputID>):Bool
		return checkAny(buttonsArray, JUST_RELEASED);

	public inline function anyReleased(buttonsArray:Array<MobileInputID>):Bool
		return checkAny(buttonsArray, RELEASED);

	/** Checks an ID without exposing the internal button layout. */
	public function checkStatus(button:MobileInputID, state:ButtonsStates = JUST_PRESSED):Bool
	{
		if (!isInputLayerActive() || button == MobileInputID.NONE) return false;

		if (button == MobileInputID.ANY)
		{
			for (id in bindings.keys())
				if (checkBoundButtons(id, state)) return true;
			return false;
		}

		return checkBoundButtons(button, state);
	}

	/** Script-friendly form of `checkStatus`. */
	public inline function checkStatusByName(name:String, state:ButtonsStates = JUST_PRESSED):Bool
		return checkStatus(MobileInputID.fromString(name), state);

	/** Script-friendly numeric form; unknown values never activate a button. */
	public inline function checkStatusByValue(value:Int, state:ButtonsStates = JUST_PRESSED):Bool
		return checkStatus(MobileInputID.fromValue(value), state);

	/**
	 * Dynamic scripting entry point available directly as `touchPad.checkMobile`
	 * or `hitbox.checkMobile`; no enum import is required by the script.
	 */
	public function checkMobile(id:Dynamic, stateName:String = "justPressed"):Bool
	{
		var resolved:MobileInputID;
		if (Std.isOfType(id, String)) resolved = MobileInputID.fromString(cast id);
		else if (Std.isOfType(id, Int)) resolved = MobileInputID.fromValue(cast id);
		else return false;
		return checkStatus(resolved, parseStateName(stateName));
	}

	public inline function hasBinding(button:MobileInputID):Bool
		return bindings.exists(button);

	public inline function isInputLayerActive():Bool
		return exists && active && visible;

	function checkAny(buttonsArray:Array<MobileInputID>, state:ButtonsStates):Bool
	{
		if (buttonsArray == null) return false;
		for (button in buttonsArray)
			if (checkStatus(button, state)) return true;
		return false;
	}

	function checkBoundButtons(id:MobileInputID, state:ButtonsStates):Bool
	{
		var candidates = bindings.get(id);
		if (candidates == null) return false;

		for (button in candidates)
		{
			if (button == null || !button.exists || !button.active || !button.visible) continue;
			if (readButtonState(button, state)) return true;
		}
		return false;
	}

	static inline function readButtonState(button:TouchButton, state:ButtonsStates):Bool
	{
		return switch (state)
		{
			case PRESSED: button.pressed;
			case JUST_PRESSED: button.justPressed;
			case RELEASED: button.released;
			case JUST_RELEASED: button.justReleased;
		};
	}

	static function parseStateName(name:String):ButtonsStates
	{
		if (name == null) return JUST_PRESSED;
		return switch (StringTools.replace(StringTools.replace(StringTools.trim(name).toLowerCase(), "_", ""), "-", ""))
		{
			case "pressed" | "held" | "hold": PRESSED;
			case "released": RELEASED;
			case "justreleased" | "up": JUST_RELEASED;
			default: JUST_PRESSED;
		};
	}

	/**
	 * Rebuilds bindings from the buttons currently in this group. Layout classes
	 * call this after their declarative button list has been assembled.
	 */
	public function updateTrackedButtons():Void
	{
		trackedButtons.clear();
		bindings.clear();

		forEachExists(function(button:TouchButton)
		{
			if (button == null || button.IDs == null) return;
			for (id in button.IDs)
			{
				if (id == MobileInputID.NONE || id == MobileInputID.ANY) continue;

				var candidates = bindings.get(id);
				if (candidates == null)
				{
					candidates = [];
					bindings.set(id, candidates);
					trackedButtons.set(id, button);
				}
				if (!candidates.contains(button)) candidates.push(button);
			}
		});
	}

	/** Prevents the closing layer's touch from pressing this newly active layer. */
	public function blockUntilTouchesReleased():Void
	{
		forEachExists(function(button:TouchButton)
		{
			if (button != null) button.blockUntilTouchesReleased();
		});
	}

	/** True when the mouse-compatible mobile pointer is over a usable button. */
	public function pointerOverActiveButton():Bool
	{
		if (!isInputLayerActive()) return false;

		var found = false;
		forEachExists(function(button:TouchButton)
		{
			if (found || button == null || !button.active || !button.visible) return;
			var targetCameras = button.cameras;
			if (targetCameras == null || targetCameras.length == 0)
			{
				if (button.camera != null && FlxG.mouse.overlaps(button, button.camera)) found = true;
				return;
			}
			for (camera in targetCameras)
			{
				if (camera != null && FlxG.mouse.overlaps(button, camera))
				{
					found = true;
					break;
				}
			}
		});
		return found;
	}
}

enum ButtonsStates
{
	PRESSED;
	JUST_PRESSED;
	RELEASED;
	JUST_RELEASED;
}
#end
