package codename.mobile;

#if TOUCH_CONTROLS
import codename.funkin.backend.MusicBeatState;
import flixel.FlxG;

/**
 * Temporarily replaces a state's pad and restores it when a nested screen
 * closes. This keeps ownership explicit and prevents nested option menus from
 * duplicating camera cleanup logic.
 */
class MobilePadLease
{
	public final previous:Array<String>;
	final owner:MusicBeatState;
	var released:Bool = false;

	public static function replaceForScreen(owner:MusicBeatState, layout:Array<String>):Null<MobilePadLease>
	{
		if (owner == null || owner.touchPad == null || layout == null || layout.length < 2) return null;
		return new MobilePadLease(owner, layout[0], layout[1]);
	}

	function new(owner:MusicBeatState, dpad:String, actions:String)
	{
		this.owner = owner;
		previous = [owner.touchPad.curDPadMode, owner.touchPad.curActionMode];
		install(dpad, actions);
	}

	public function release():Void
	{
		if (released) return;
		released = true;
		if (FlxG.state == owner && owner.exists) install(previous[0], previous[1]);
	}

	function install(dpad:String, actions:String):Void
	{
		owner.addTouchPad(dpad, actions);
		owner.addTouchPadCamera();
		if (owner.touchPad != null) owner.touchPad.blockUntilTouchesReleased();
	}
}
#end
