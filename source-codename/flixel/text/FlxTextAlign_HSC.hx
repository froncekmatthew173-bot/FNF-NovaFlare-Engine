package flixel.text;

#if CODENAME_ENGINE_COMPAT
import flixel.text.FlxText.FlxTextAlign;
import openfl.text.TextFormatAlign;

/** Runtime reflection surface for the FlxTextAlign abstract used by HScript. */
class FlxTextAlign_HSC
{
	public static final LEFT:FlxTextAlign = FlxTextAlign.LEFT;
	public static final CENTER:FlxTextAlign = FlxTextAlign.CENTER;
	public static final RIGHT:FlxTextAlign = FlxTextAlign.RIGHT;
	public static final JUSTIFY:FlxTextAlign = FlxTextAlign.JUSTIFY;

	public static inline function fromOpenFL(align:TextFormatAlign):FlxTextAlign
		return FlxTextAlign.fromOpenFL(align);

	public static inline function toOpenFL(align:FlxTextAlign):TextFormatAlign
		return FlxTextAlign.toOpenFL(align);
}
#end
