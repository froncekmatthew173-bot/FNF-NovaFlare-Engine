package codename.mobile;

#if mobile
import codename.funkin.backend.MusicBeatState;
import codename.funkin.backend.MusicBeatSubstate;
import codename.funkin.backend.MusicBeatTransition;
import codename.funkin.backend.system.Controls.Control;
import flixel.FlxG;
import flixel.FlxState;
import flixel.input.FlxInput.FlxInputState;
import haxe.ds.Map;
import mobile.funkin.backend.system.input.MobileInputID;
import mobile.funkin.backend.system.input.MobileInputManager;

using StringTools;

/**
 * Adapts Codename actions to the active mobile-control layer.
 *
 * Routing is deliberately exclusive: the deepest open substate owns mobile
 * input. If that substate has no mobile controls, input is blocked instead of
 * leaking into the state underneath it. Keyboard and gamepad actions continue
 * to be combined by Codename's Controls getters.
 */
class CodeNameMobileInput
{
	static var controlBindings:Map<Control, MobileInputID> = createControlBindings();
	static var namedControls:Map<String, Control> = createNamedControls();
	static var consumerStack:Array<FlxState> = [];

	static function createControlBindings():Map<Control, MobileInputID>
	{
		var result = new Map<Control, MobileInputID>();
		result.set(Control.UP, MobileInputID.UP);
		result.set(Control.DOWN, MobileInputID.DOWN);
		result.set(Control.LEFT, MobileInputID.LEFT);
		result.set(Control.RIGHT, MobileInputID.RIGHT);
		result.set(Control.NOTE_UP, MobileInputID.HITBOX_UP);
		result.set(Control.NOTE_DOWN, MobileInputID.HITBOX_DOWN);
		result.set(Control.NOTE_LEFT, MobileInputID.HITBOX_LEFT);
		result.set(Control.NOTE_RIGHT, MobileInputID.HITBOX_RIGHT);
		result.set(Control.ACCEPT, MobileInputID.A);
		result.set(Control.BACK, MobileInputID.B);
		result.set(Control.PAUSE, MobileInputID.P);
		result.set(Control.RESET, MobileInputID.C);
		result.set(Control.CHANGE_MODE, MobileInputID.X);
		result.set(Control.SWITCHMOD, MobileInputID.M);
		result.set(Control.DEV_ACCESS, MobileInputID.E);
		// Y is a visible Freeplay extension button. Developer reload remains a
		// keyboard/gamepad action so an accidental screen tap cannot reset a menu.
		return result;
	}

	/** Marks which state is currently evaluating its Controls getters. */
	public static function beginConsumer(owner:FlxState):Void
	{
		consumerStack.push(owner);
	}

	public static function endConsumer(owner:FlxState):Void
	{
		if (consumerStack.length == 0) return;
		if (consumerStack[consumerStack.length - 1] == owner)
			consumerStack.pop();
		else
			consumerStack.remove(owner);
	}

	static function createNamedControls():Map<String, Control>
	{
		var result = new Map<String, Control>();
		bindNames(result, Control.UP, ["up"]);
		bindNames(result, Control.DOWN, ["down"]);
		bindNames(result, Control.LEFT, ["left"]);
		bindNames(result, Control.RIGHT, ["right"]);
		bindNames(result, Control.NOTE_UP, ["note-up", "note_up"]);
		bindNames(result, Control.NOTE_DOWN, ["note-down", "note_down"]);
		bindNames(result, Control.NOTE_LEFT, ["note-left", "note_left"]);
		bindNames(result, Control.NOTE_RIGHT, ["note-right", "note_right"]);
		bindNames(result, Control.ACCEPT, ["accept"]);
		bindNames(result, Control.BACK, ["back"]);
		bindNames(result, Control.PAUSE, ["pause"]);
		bindNames(result, Control.RESET, ["reset"]);
		bindNames(result, Control.CHANGE_MODE, ["change-mode", "change_mode"]);
		bindNames(result, Control.SWITCHMOD, ["switchmod"]);
		bindNames(result, Control.FPS_COUNTER, ["fps-counter", "fps_counter"]);
		bindNames(result, Control.DEV_ACCESS, ["dev-access", "dev_access"]);
		bindNames(result, Control.DEV_CONSOLE, ["dev-console", "dev_console"]);
		bindNames(result, Control.DEV_RELOAD, ["dev-reload", "dev_reload"]);
		return result;
	}

	static function bindNames(table:Map<String, Control>, control:Control, names:Array<String>):Void
	{
		for (name in names) table.set(normalizeControlName(name), control);
	}

	static inline function normalizeControlName(name:String):String
		return name == null ? "" : name.trim().toLowerCase();

	public static function check(control:Control, state:FlxInputState):Bool
	{
		if (!currentConsumerOwnsInput()) return false;
		#if android
		// Lime exposes Android BACK on release; retain Codename's just-pressed
		// action semantics without replacing keyboard/gamepad bindings.
		if (control == Control.BACK && state == JUST_PRESSED && FlxG.android.justReleased.BACK)
			return true;
		#end

		return checkID(getID(control), state);
	}

	public static function checkName(name:String, state:FlxInputState):Bool
	{
		var control = namedControls.get(normalizeControlName(name));
		return control != null && check(control, state);
	}

	/** Checks any virtual-pad or hitbox ID in the current topmost layer. */
	public static function checkID(id:MobileInputID, state:FlxInputState = JUST_PRESSED):Bool
	{
		if (id == MobileInputID.NONE || !currentConsumerOwnsInput()) return false;

		var owner = inputOwner();
		if (owner is MusicBeatSubstate)
		{
			var mobileSubstate:MusicBeatSubstate = cast owner;
			return checkLayer(mobileSubstate.touchPad, mobileSubstate.hitbox, id, state);
		}

		var mobileState:MusicBeatState = Std.downcast(owner, MusicBeatState);
		return mobileState != null && checkLayer(mobileState.touchPad, mobileState.hitbox, id, state);
	}

	public static inline function checkIDName(name:String, state:FlxInputState = JUST_PRESSED):Bool
		return checkID(MobileInputID.fromString(name), state);

	public static inline function checkIDValue(value:Int, state:FlxInputState = JUST_PRESSED):Bool
		return checkID(MobileInputID.fromValue(value), state);

	/**
	 * Dynamic scripting entry point. `id` accepts an ID name or stable integer;
	 * `stateName` accepts pressed, justPressed, released or justReleased.
	 */
	public static function checkMobile(id:Dynamic, stateName:String = "justPressed"):Bool
	{
		var resolved:MobileInputID;
		if (Std.isOfType(id, String)) resolved = MobileInputID.fromString(cast id);
		else if (Std.isOfType(id, Int)) resolved = MobileInputID.fromValue(cast id);
		else return false;
		return checkID(resolved, parseState(stateName));
	}

	public static function pointerOverActiveControl():Bool
	{
		if (!currentConsumerOwnsInput()) return false;
		var owner = inputOwner();
		if (owner is MusicBeatSubstate)
		{
			var mobileSubstate:MusicBeatSubstate = cast owner;
			return pointerOverLayer(mobileSubstate.touchPad, mobileSubstate.hitbox);
		}

		var mobileState:MusicBeatState = Std.downcast(owner, MusicBeatState);
		return mobileState != null && pointerOverLayer(mobileState.touchPad, mobileState.hitbox);
	}

	public static function getID(control:Control):MobileInputID
	{
		var id = controlBindings.get(control);
		return id != null ? id : MobileInputID.NONE;
	}

	static function checkLayer(touchPad:MobileInputManager, hitbox:MobileInputManager,
		id:MobileInputID, state:FlxInputState):Bool
	{
		return checkManager(touchPad, id, state) || checkManager(hitbox, id, state);
	}

	static function checkManager(manager:MobileInputManager, id:MobileInputID, state:FlxInputState):Bool
	{
		if (manager == null || !manager.isInputLayerActive()) return false;
		return switch (state)
		{
			case PRESSED: manager.buttonPressed(id);
			case JUST_PRESSED: manager.buttonJustPressed(id);
			case RELEASED: manager.buttonReleased(id);
			case JUST_RELEASED: manager.buttonJustReleased(id);
			default: false;
		};
	}

	static inline function pointerOverLayer(touchPad:MobileInputManager, hitbox:MobileInputManager):Bool
	{
		return (touchPad != null && touchPad.pointerOverActiveButton())
			|| (hitbox != null && hitbox.pointerOverActiveButton());
	}

	/**
	 * Returns the layer that owns touch controls. Visual transition substates are
	 * transparent: they deliberately create no pad and must not swallow the
	 * parent layer's input while its buttons remain visible.
	 */
	static function inputOwner():FlxState
	{
		if (FlxG.state == null) return null;
		var owner:FlxState = FlxG.state;
		var current = FlxG.state.subState;
		while (current != null)
		{
			if (!(current is MusicBeatTransition)) owner = current;
			current = current.subState;
		}
		return owner;
	}

	static function currentConsumerOwnsInput():Bool
	{
		if (consumerStack.length == 0) return true;
		var consumer:FlxState = consumerStack[consumerStack.length - 1];
		var owner:FlxState = inputOwner();
		return owner != null && consumer == owner;
	}

	static function parseState(name:String):FlxInputState
	{
		if (name == null) return JUST_PRESSED;
		return switch (name.trim().toLowerCase().replace("_", "").replace("-", ""))
		{
			case "pressed" | "held" | "hold": PRESSED;
			case "released": RELEASED;
			case "justreleased" | "up": JUST_RELEASED;
			default: JUST_PRESSED;
		};
	}
}
#end
