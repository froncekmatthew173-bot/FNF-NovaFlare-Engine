package originfunkin;

import funkin.save.Save;

class OriginFpsDisplay
{
  public static function applySavedScale():Void
  {
    applyScale(Save.instance.options.novaSettings?.fpsViewScale ?? 1.0);
  }

  public static function applyScale(scale:Float):Void
  {
    #if sys
    if (!Math.isFinite(scale)) scale = 1.0;
    scale = flixel.math.FlxMath.bound(scale, 0.5, 2.0);
    if (Main.fpsVar != null)
    {
      Main.fpsVar.scaleX = scale;
      Main.fpsVar.scaleY = scale;
    }
    #end
  }
}
