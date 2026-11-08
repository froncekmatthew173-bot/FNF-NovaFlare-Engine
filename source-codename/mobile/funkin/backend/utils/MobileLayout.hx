package mobile.funkin.backend.utils;

#if TOUCH_CONTROLS
import mobile.funkin.backend.system.input.MobileInputID;

/** Screen edge used to resolve a slot at the current logical resolution. */
enum abstract MobileAnchor(Int) from Int to Int
{
	var TOP_LEFT = 0;
	var TOP_RIGHT = 1;
	var BOTTOM_LEFT = 2;
	var BOTTOM_RIGHT = 3;
}

/**
 * Typed description of one virtual button. Offsets are measured from the
 * selected screen edge, so the 1280x720 layout keeps its shape if the logical
 * game size ever changes.
 */
final class MobileButtonSlot
{
	public final input:MobileInputID;
	public final symbol:String;
	public final anchor:MobileAnchor;
	public final horizontalOffset:Float;
	public final verticalOffset:Float;
	public final tint:Int;

	public function new(input:MobileInputID, symbol:String, anchor:MobileAnchor,
		horizontalOffset:Float, verticalOffset:Float, tint:Int)
	{
		this.input = input;
		this.symbol = symbol;
		this.anchor = anchor;
		this.horizontalOffset = horizontalOffset;
		this.verticalOffset = verticalOffset;
		this.tint = tint;
	}

	public inline function resolveX(screenWidth:Float):Float
	{
		return switch (anchor)
		{
			case TOP_RIGHT | BOTTOM_RIGHT: screenWidth - horizontalOffset;
			case TOP_LEFT | BOTTOM_LEFT: horizontalOffset;
		}
	}

	public inline function resolveY(screenHeight:Float):Float
	{
		return switch (anchor)
		{
			case BOTTOM_LEFT | BOTTOM_RIGHT: screenHeight - verticalOffset;
			case TOP_LEFT | TOP_RIGHT: verticalOffset;
		}
	}
}

/** Immutable collection of slots used by one named TouchPad mode. */
final class MobileLayout
{
	public final id:String;
	public final slots:Array<MobileButtonSlot>;

	public function new(id:String, slots:Array<MobileButtonSlot>)
	{
		this.id = id;
		this.slots = slots;
	}
}
#end
