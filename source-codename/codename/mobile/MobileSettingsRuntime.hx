package codename.mobile;

#if TOUCH_CONTROLS
import codename.funkin.backend.MusicBeatState;
import codename.funkin.options.Options;
import codename.funkin.options.PlayerSettings;
import flixel.FlxG;
#if mobile
import lime.system.System as LimeSystem;
#end

/** Applies live setting changes without making the settings UI own controls. */
class MobileSettingsRuntime
{
	public static function reloadPadGraphics():Void
	{
		var owner = currentOwner();
		if (owner == null || owner.touchPad == null) return;

		var dpad = owner.touchPad.curDPadMode;
		var actions = owner.touchPad.curActionMode;
		owner.addTouchPad(dpad, actions);
		owner.addTouchPadCamera();

		if (owner.touchPad != null)
		{
			owner.touchPad.alpha = Options.touchPadAlpha;
			owner.touchPad.blockUntilTouchesReleased();
		}
		syncPointerVisibility();
	}

	public static function applyPadOpacity(value:Float):Void
	{
		var owner = currentOwner();
		if (owner != null && owner.touchPad != null) owner.touchPad.alpha = value;
		syncPointerVisibility();
	}

	public static function applyScreenTimeout():Void
	{
		#if mobile
		LimeSystem.allowScreenTimeout = Options.screenTimeOut;
		#end
	}

	static inline function currentOwner():Null<MusicBeatState>
		return Std.downcast(FlxG.state, MusicBeatState);

	static function syncPointerVisibility():Void
	{
		var showPointer = !PlayerSettings.solo.controls.touchC;
		FlxG.mouse.visible = showPointer;
		FlxG.mouse.useSystemCursor = showPointer;
	}
}
#end
