package originfunkin.input;

/** Touch motion owned by Freeplay; NF's pointer update loop stays unchanged. */
class FreeplayTouchGesture
{
  public var deltaY(default, null):Float = 0;
  public var dragged(default, null):Bool = false;
  public var swipeLeft(default, null):Bool = false;
  public var swipeRight(default, null):Bool = false;
  public var flickLeft(default, null):Bool = false;
  public var flickRight(default, null):Bool = false;
  // Selection units per second: the menu's existing scale is 60 pixels per song.
  public var momentumY(default, null):Float = 0;
  public var flicking(get, never):Bool;

  var active:Bool = false;
  var contactID:Int = -1;
  var pressTime:Int = -1;
  var startX:Float = 0;
  var startY:Float = 0;
  var previousY:Float = 0;
  var swipeX:Float = 0;
  var velocityY:Float = 0;
  var idleTime:Float = 0;
  var duration:Float = 0;

  public function new() {}

  public function update(id:Int, startedAt:Int, pressed:Bool, justPressed:Bool, justReleased:Bool,
    x:Float, y:Float, elapsed:Float, horizontalThreshold:Float):Void
  {
    deltaY = 0;
    swipeLeft = swipeRight = flickLeft = flickRight = false;
    if (!Math.isFinite(x) || !Math.isFinite(y) || !Math.isFinite(elapsed) || elapsed <= 0)
    {
      active = false;
      stopMomentum();
      return;
    }

    if (pressed && (!active || justPressed || id != contactID || startedAt != pressTime))
    {
      active = true;
      contactID = id;
      pressTime = startedAt;
      startX = swipeX = x;
      startY = previousY = y;
      dragged = false;
      velocityY = idleTime = duration = 0;
      stopMomentum();
      return;
    }

    if (active && id == contactID && (pressed || justReleased))
    {
      duration += elapsed;
      deltaY = y - previousY;
      previousY = y;
      if (Math.abs(x - startX) >= 10 || Math.abs(y - startY) >= 10) dragged = true;
      if (deltaY != 0)
      {
        velocityY = deltaY / (elapsed * 60);
        idleTime = 0;
      }
      else
      {
        idleTime += elapsed;
      }

      // Keep Origin's menu navigation direction, relative to the press position.
      if (x - swipeX > horizontalThreshold)
      {
        swipeLeft = true;
        swipeX = x;
      }
      else if (x - swipeX < -horizontalThreshold)
      {
        swipeRight = true;
        swipeX = x;
      }

      if (justReleased)
      {
        active = false;
        if (dragged && idleTime <= 0.05 && Math.abs(velocityY) > 10) momentumY = velocityY;
        flickLeft = duration <= 0.3 && x - startX <= -60;
        flickRight = duration <= 0.3 && x - startX >= 60;
      }
    }
    else
    {
      active = false;
      deltaY = 0;
      if (pressed) stopMomentum();
      else
      {
        momentumY *= Math.pow(0.95, elapsed * 60);
        if (Math.abs(momentumY) <= 1) stopMomentum();
      }
    }
  }

  public function stopMomentum():Void
  {
    momentumY = 0;
  }

  inline function get_flicking():Bool return momentumY != 0;
}
