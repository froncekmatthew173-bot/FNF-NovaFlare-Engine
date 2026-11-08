package funkin.graphics;

import flixel.FlxCamera;
import flixel.FlxG;
import flixel.system.frontEnds.CameraFrontEnd;

/**
 * A `CameraFrontEnd` override that uses `FunkinCamera`!
 */
@:nullSafety
@:access(flixel.system.frontEnds.CameraFrontEnd)
class FunkinCameraFrontEnd extends CameraFrontEnd
{
  /** Scripts may destroy a camera before the state switch resets the camera list. */
  @:nullSafety(Off)
  override public function remove(camera:FlxCamera, destroy:Bool = true):Void
  {
    if (list.contains(camera) && (camera == null || camera.flashSprite == null))
    {
      // A destroyed camera has no display sprite and must not be destroyed again.
      // Preserve the arrays used by Flixel's default draw targets.
      while (list.remove(camera)) {}
      while (defaults.remove(camera)) {}
      for (i => remaining in list)
        if (remaining != null) remaining.ID = i;
      if (camera != null)
      {
        FlxG.log.warn('[originFunkin] Removed an already destroyed camera from the active camera list.');
        cameraRemoved.dispatch(camera);
      }
      return;
    }

    super.remove(camera, destroy);
  }

  override public function reset(?newCamera:FlxCamera):Void
  {
    super.reset(newCamera ?? new FunkinCamera());
  }
}
