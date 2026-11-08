package codename.mobile;

#if TOUCH_CONTROLS
import flixel.FlxCamera;
import flixel.FlxG;
import flixel.FlxState;
import flixel.util.FlxDestroyUtil;
import mobile.objects.Hitbox;
import mobile.objects.TouchPad;

/** Owns the visual controls and their cameras for one state layer. */
class MobileControlLayer
{
	public var touchPad(default, null):TouchPad;
	public var hitbox(default, null):Hitbox;
	public var touchPadCamera(default, null):FlxCamera;
	public var hitboxCamera(default, null):FlxCamera;

	final owner:FlxState;

	public function new(owner:FlxState)
	{
		this.owner = owner;
	}

	public function setTouchPad(dpad:String, actions:String):TouchPad
	{
		clearTouchPad();
		touchPad = new TouchPad(dpad, actions);
		owner.add(touchPad);
		return touchPad;
	}

	public function setHitbox(extraMode:String, defaultDrawTarget:Bool = false):Hitbox
	{
		clearHitbox();
		hitbox = new Hitbox(extraMode);
		hitboxCamera = transparentCamera(defaultDrawTarget);
		hitbox.cameras = [hitboxCamera];
		owner.add(hitbox);
		return hitbox;
	}

	public function giveTouchPadCamera(defaultDrawTarget:Bool = false):FlxCamera
	{
		if (touchPad == null) return null;
		removeCamera(touchPadCamera);
		touchPadCamera = transparentCamera(defaultDrawTarget);
		touchPad.cameras = [touchPadCamera];
		return touchPadCamera;
	}

	public function apply(scheme:MobileControlScheme, extraMode:String):Void
	{
		if (scheme == null) return;
		if (scheme.gameplayHitbox) setHitbox(extraMode);
		setTouchPad(scheme.dpad, scheme.actions);
		if (scheme.separateCamera) giveTouchPadCamera();
	}

	public function updateAvailability(show:Bool, acceptTouchPad:Bool, noChildState:Bool):Void
	{
		if (touchPad != null)
		{
			touchPad.visible = show && acceptTouchPad;
			touchPad.active = show && noChildState && acceptTouchPad;
		}
		if (hitbox != null)
		{
			hitbox.visible = show;
			hitbox.active = show && noChildState;
		}
	}

	public function raise():Void
	{
		moveObjectToTop(hitbox);
		moveObjectToTop(touchPad);
		moveCameraToTop(hitboxCamera);
		moveCameraToTop(touchPadCamera);
	}

	public function blockCurrentTouches():Void
	{
		if (touchPad != null) touchPad.blockUntilTouchesReleased();
		if (hitbox != null) hitbox.blockUntilTouchesReleased();
	}

	public function clearTouchPad():Void
	{
		if (touchPad != null)
		{
			owner.remove(touchPad, true);
			touchPad = FlxDestroyUtil.destroy(touchPad);
		}
		removeCamera(touchPadCamera);
		touchPadCamera = null;
	}

	public function clearHitbox():Void
	{
		if (hitbox != null)
		{
			owner.remove(hitbox, true);
			hitbox = FlxDestroyUtil.destroy(hitbox);
		}
		removeCamera(hitboxCamera);
		hitboxCamera = null;
	}

	public function destroy():Void
	{
		clearTouchPad();
		clearHitbox();
	}

	function transparentCamera(defaultDrawTarget:Bool):FlxCamera
	{
		var camera = new FlxCamera();
		camera.bgColor = 0;
		FlxG.cameras.add(camera, defaultDrawTarget);
		return camera;
	}

	function moveObjectToTop(object:Dynamic):Void
	{
		if (object == null || !owner.members.contains(object)) return;
		owner.remove(object, true);
		owner.add(object);
	}

	function moveCameraToTop(camera:FlxCamera):Void
	{
		if (camera == null || !FlxG.cameras.list.contains(camera)) return;
		FlxG.cameras.remove(camera, false);
		FlxG.cameras.add(camera, false);
	}

	function removeCamera(camera:FlxCamera):Void
	{
		if (camera == null) return;
		if (FlxG.cameras.list.contains(camera)) FlxG.cameras.remove(camera, false);
		camera.destroy();
	}
}
#end
