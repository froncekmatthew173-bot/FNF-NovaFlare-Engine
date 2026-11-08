package crowplexus.hscript.proxy.flixel.util;

import flixel.system.macros.FlxMacroUtil;

import crowplexus.hscript.ISharedScript;

/**
 * 用于代理script里的FlxColor，肥肠还原（除了from）
 */
class ProxyFlxColor implements ISharedScript {
	// 他们没告诉我QAQ，我是傻逼
	public static var TRANSPARENT(get, never):ProxyFlxColor;
	static function get_TRANSPARENT():ProxyFlxColor {
		return new ProxyFlxColor(0x00000000);
	}
	public static var WHITE(get, never):ProxyFlxColor;
	static function get_WHITE():ProxyFlxColor {
		return new ProxyFlxColor(0xFFFFFFFF);
	}
	public static var GRAY(get, never):ProxyFlxColor;
	static function get_GRAY():ProxyFlxColor {
		return new ProxyFlxColor(0xFF808080);
	}
	public static var BLACK(get, never):ProxyFlxColor;
	static function get_BLACK():ProxyFlxColor {
		return new ProxyFlxColor(0xFF000000);
	}

	public static var GREEN(get, never):ProxyFlxColor;
	static function get_GREEN():ProxyFlxColor {
		return new ProxyFlxColor(0xFF008000);
	}
	public static var LIME(get, never):ProxyFlxColor;
	static function get_LIME():ProxyFlxColor {
		return new ProxyFlxColor(0xFF00FF00);
	}
	public static var YELLOW(get, never):ProxyFlxColor;
	static function get_YELLOW():ProxyFlxColor {
		return new ProxyFlxColor(0xFFFFFF00);
	}
	public static var ORANGE(get, never):ProxyFlxColor;
	static function get_ORANGE():ProxyFlxColor {
		return new ProxyFlxColor(0xFFFFA500);
	}
	public static var RED(get, never):ProxyFlxColor;
	static function get_RED():ProxyFlxColor {
		return new ProxyFlxColor(0xFFFF0000);
	}
	public static var PURPLE(get, never):ProxyFlxColor;
	static function get_PURPLE():ProxyFlxColor {
		return new ProxyFlxColor(0xFF800080);
	}
	public static var BLUE(get, never):ProxyFlxColor;
	static function get_BLUE():ProxyFlxColor {
		return new ProxyFlxColor(0xFF0000FF);
	}
	public static var BROWN(get, never):ProxyFlxColor;
	static function get_BROWN():ProxyFlxColor {
		return new ProxyFlxColor(0xFF8B4513);
	}
	public static var PINK(get, never):ProxyFlxColor;
	static function get_PINK():ProxyFlxColor {
		return new ProxyFlxColor(0xFFFFC0CB);
	}
	public static var MAGENTA(get, never):ProxyFlxColor;
	static function get_MAGENTA():ProxyFlxColor {
		return new ProxyFlxColor(0xFFFF00FF);
	}
	public static var CYAN(get, never):ProxyFlxColor;
	static function get_CYAN():ProxyFlxColor {
		return new ProxyFlxColor(0xFF00FFFF);
	}

	public static var colorLookup(get, never):Map<String, Int>;
	static inline function get_colorLookup():Map<String, Int> {
		return FlxColor.colorLookup;
	}

	public var red(get, set):Int;
	public var blue(get, set):Int;
	public var green(get, set):Int;
	public var alpha(get, set):Int;

	public var redFloat(get, set):Float;
	public var blueFloat(get, set):Float;
	public var greenFloat(get, set):Float;
	public var alphaFloat(get, set):Float;

	public var cyan(get, set):Float;
	public var magenta(get, set):Float;
	public var yellow(get, set):Float;
	public var black(get, set):Float;

	public var rgb(get, set):ProxyFlxColor;

	public var hue(get, set):Float;

	public var saturation(get, set):Float;

	public var brightness(get, set):Float;

	public var lightness(get, set):Float;

	static var COLOR_REGEX = ~/^(0x|#)(([A-F0-9]{2}){3,4})$/i;

	public static function fromInt(Value:Int):ProxyFlxColor
	{
		return new ProxyFlxColor(Value);
	}

	public static function fromRGB(Red:Int, Green:Int, Blue:Int, Alpha:Int = 255):ProxyFlxColor
	{
		var color = new ProxyFlxColor();
		return color.setRGB(Red, Green, Blue, Alpha);
	}

	public static function fromRGBFloat(Red:Float, Green:Float, Blue:Float, Alpha:Float = 1):ProxyFlxColor
	{
		var color = new ProxyFlxColor();
		return color.setRGBFloat(Red, Green, Blue, Alpha);
	}

	public static function fromCMYK(Cyan:Float, Magenta:Float, Yellow:Float, Black:Float, Alpha:Float = 1):ProxyFlxColor
	{
		var color = new ProxyFlxColor();
		return color.setCMYK(Cyan, Magenta, Yellow, Black, Alpha);
	}

	public static function fromHSB(Hue:Float, Saturation:Float, Brightness:Float, Alpha:Float = 1):ProxyFlxColor
	{
		var color = new ProxyFlxColor();
		return color.setHSB(Hue, Saturation, Brightness, Alpha);
	}

	public static function fromHSL(Hue:Float, Saturation:Float, Lightness:Float, Alpha:Float = 1):ProxyFlxColor
	{
		var color = new ProxyFlxColor();
		return color.setHSL(Hue, Saturation, Lightness, Alpha);
	}

	public static function fromString(str:String):Null<ProxyFlxColor>
	{
		var result:Null<ProxyFlxColor> = null;
		str = StringTools.trim(str);

		if (COLOR_REGEX.match(str))
		{
			var hexColor:String = "0x" + COLOR_REGEX.matched(2);
			result = new ProxyFlxColor(Std.parseInt(hexColor));
			if (hexColor.length == 8)
			{
				result.alphaFloat = 1;
			}
		}
		else
		{
			str = str.toUpperCase();
			for (key in colorLookup.keys())
			{
				if (key.toUpperCase() == str)
				{
					result = new ProxyFlxColor(colorLookup.get(key));
					break;
				}
			}
		}

		return result;
	}

	public static function getHSBColorWheel(Alpha:Int = 255):Array<ProxyFlxColor>
	{
		return [for (c in 0...360) fromHSB(c, 1.0, 1.0, Alpha)];
	}

	public static function interpolate(Color1:ProxyFlxColor, Color2:ProxyFlxColor, Factor:Float = 0.5):ProxyFlxColor
	{
		var r:Int = Std.int((Color2.red - Color1.red) * Factor + Color1.red);
		var g:Int = Std.int((Color2.green - Color1.green) * Factor + Color1.green);
		var b:Int = Std.int((Color2.blue - Color1.blue) * Factor + Color1.blue);
		var a:Int = Std.int((Color2.alpha - Color1.alpha) * Factor + Color1.alpha);

		return fromRGB(r, g, b, a);
	}

	public static function gradient(Color1:ProxyFlxColor, Color2:ProxyFlxColor, Steps:Int, ?Ease:Float->Float):Array<ProxyFlxColor>
	{
		var output = new Array<ProxyFlxColor>();

		if (Ease == null)
		{
			Ease = function(t:Float):Float
			{
				return t;
			}
		}

		for (step in 0...Steps)
		{
			output[step] = interpolate(Color1, Color2, Ease(step / (Steps - 1)));
		}

		return output;
	}

	public function getComplementHarmony():ProxyFlxColor
	{
		return fromHSB(FlxMath.wrap(Std.int(hue) + 180, 0, 350), brightness, saturation, alphaFloat);
	}

	public function to24Bit():ProxyFlxColor
	{
		return new ProxyFlxColor(this._color & 0xffffff);
	}

	public function toHexString(Alpha:Bool = true, Prefix:Bool = true):String
	{
		return (Prefix ? "0x" : "") + (Alpha ? StringTools.hex(alpha,
			2) : "") + StringTools.hex(red, 2) + StringTools.hex(green, 2) + StringTools.hex(blue, 2);
	}

	public function toWebString():String
	{
		return "#" + toHexString(false, false);
	}

	public function getColorInfo():String
	{
		// Hex format
		var result:String = toHexString() + "\n";
		// RGB format
		result += "Alpha: " + alpha + " Red: " + red + " Green: " + green + " Blue: " + blue + "\n";
		// HSB/HSL info
		result += "Hue: " + FlxMath.roundDecimal(hue, 2) + " Saturation: " + FlxMath.roundDecimal(saturation, 2) + " Brightness: "
			+ FlxMath.roundDecimal(brightness, 2) + " Lightness: " + FlxMath.roundDecimal(lightness, 2);

		return result;
	}

	public function getDarkened(Factor:Float = 0.2):ProxyFlxColor
	{
		Factor = FlxMath.bound(Factor, 0, 1);
		var output:ProxyFlxColor = this;
		output.lightness = output.lightness * (1 - Factor);
		return output;
	}

	public function getLightened(Factor:Float = 0.2):ProxyFlxColor
	{
		Factor = FlxMath.bound(Factor, 0, 1);
		var output:ProxyFlxColor = this;
		output.lightness = output.lightness + (1 - lightness) * Factor;
		return output;
	}

	public function getInverted():ProxyFlxColor
	{
		var oldAlpha = alpha;
		var output:ProxyFlxColor = subtract(ProxyFlxColor.WHITE, this);
		output.alpha = oldAlpha;
		return output;
	}

	public function setRGB(Red:Int, Green:Int, Blue:Int, Alpha:Int = 255):ProxyFlxColor
	{
		red = Red;
		green = Green;
		blue = Blue;
		alpha = Alpha;
		return this;
	}

	public function setRGBFloat(Red:Float, Green:Float, Blue:Float, Alpha:Float = 1):ProxyFlxColor
	{
		redFloat = Red;
		greenFloat = Green;
		blueFloat = Blue;
		alphaFloat = Alpha;
		return this;
	}

	public function setCMYK(Cyan:Float, Magenta:Float, Yellow:Float, Black:Float, Alpha:Float = 1):ProxyFlxColor
	{
		redFloat = (1 - Cyan) * (1 - Black);
		greenFloat = (1 - Magenta) * (1 - Black);
		blueFloat = (1 - Yellow) * (1 - Black);
		alphaFloat = Alpha;
		return this;
	}

	public function setHSB(Hue:Float, Saturation:Float, Brightness:Float, Alpha:Float):ProxyFlxColor
	{
		var chroma = Brightness * Saturation;
		var match = Brightness - chroma;
		return setHueChromaMatch(Hue, chroma, match, Alpha);
	}

	public function setHSL(Hue:Float, Saturation:Float, Lightness:Float, Alpha:Float):ProxyFlxColor
	{
		var chroma = (1 - Math.abs(2 * Lightness - 1)) * Saturation;
		var match = Lightness - chroma / 2;
		return setHueChromaMatch(Hue, chroma, match, Alpha);
	}

	inline function setHueChromaMatch(Hue:Float, Chroma:Float, Match:Float, Alpha:Float):ProxyFlxColor
	{
		Hue %= 360;
		var hueD = Hue / 60;
		var mid = Chroma * (1 - Math.abs(hueD % 2 - 1)) + Match;
		Chroma += Match;

		switch (Std.int(hueD))
		{
			case 0:
				setRGBFloat(Chroma, mid, Match, Alpha);
			case 1:
				setRGBFloat(mid, Chroma, Match, Alpha);
			case 2:
				setRGBFloat(Match, Chroma, mid, Alpha);
			case 3:
				setRGBFloat(Match, mid, Chroma, Alpha);
			case 4:
				setRGBFloat(mid, Match, Chroma, Alpha);
			case 5:
				setRGBFloat(Chroma, Match, mid, Alpha);
		}

		return this;
	}

	public var standard(get, never):Dynamic;
	public function get_standard(): Dynamic {
		return this._color;
	}

	var _color:Int;

	public function new(Value:Int = 0)
	{
		this._color = Value;
	}

	public function hget(name:String, ?e:Expr): Dynamic {
		if(name == "standard" || name == "get_standard" || name == "_color" || name == "hget" || name == "hset") throw "Gun~";
		return Reflect.getProperty(this, name);
	}

	public function hset(name:String, value: Dynamic, ?e:Expr): Void {
		if(name == "standard" || name == "get_standard" || name == "_color" || name == "hget" || name == "hset") throw "Gun~";
		return Reflect.setProperty(this, name, value);
	}

	inline function getThis():Int
	{
		return this._color;
	}


	inline function get_red():Int
	{
		return (getThis() >> 16) & 0xff;
	}

	inline function get_green():Int
	{
		return (getThis() >> 8) & 0xff;
	}

	inline function get_blue():Int
	{
		return getThis() & 0xff;
	}

	inline function get_alpha():Int
	{
		return (getThis() >> 24) & 0xff;
	}

	inline function get_redFloat():Float
	{
		return red / 255;
	}

	inline function get_greenFloat():Float
	{
		return green / 255;
	}

	inline function get_blueFloat():Float
	{
		return blue / 255;
	}

	inline function get_alphaFloat():Float
	{
		return alpha / 255;
	}

	inline function set_red(Value:Int):Int
	{
		this._color &= 0xff00ffff;
		this._color |= boundChannel(Value) << 16;
		return Value;
	}

	inline function set_green(Value:Int):Int
	{
		this._color &= 0xffff00ff;
		this._color |= boundChannel(Value) << 8;
		return Value;
	}

	inline function set_blue(Value:Int):Int
	{
		this._color &= 0xffffff00;
		this._color |= boundChannel(Value);
		return Value;
	}

	inline function set_alpha(Value:Int):Int
	{
		this._color &= 0x00ffffff;
		this._color |= boundChannel(Value) << 24;
		return Value;
	}

	inline function set_redFloat(Value:Float):Float
	{
		red = Math.round(Value * 255);
		return Value;
	}

	inline function set_greenFloat(Value:Float):Float
	{
		green = Math.round(Value * 255);
		return Value;
	}

	inline function set_blueFloat(Value:Float):Float
	{
		blue = Math.round(Value * 255);
		return Value;
	}

	inline function set_alphaFloat(Value:Float):Float
	{
		alpha = Math.round(Value * 255);
		return Value;
	}

	inline function get_cyan():Float
	{
		return (1 - redFloat - black) / brightness;
	}

	inline function get_magenta():Float
	{
		return (1 - greenFloat - black) / brightness;
	}

	inline function get_yellow():Float
	{
		return (1 - blueFloat - black) / brightness;
	}

	inline function get_black():Float
	{
		return 1 - brightness;
	}

	inline function set_cyan(Value:Float):Float
	{
		setCMYK(Value, magenta, yellow, black, alphaFloat);
		return Value;
	}

	inline function set_magenta(Value:Float):Float
	{
		setCMYK(cyan, Value, yellow, black, alphaFloat);
		return Value;
	}

	inline function set_yellow(Value:Float):Float
	{
		setCMYK(cyan, magenta, Value, black, alphaFloat);
		return Value;
	}

	inline function set_black(Value:Float):Float
	{
		setCMYK(cyan, magenta, yellow, Value, alphaFloat);
		return Value;
	}

	function get_hue():Float
	{
		var hueRad = Math.atan2(Math.sqrt(3) * (greenFloat - blueFloat), 2 * redFloat - greenFloat - blueFloat);
		var hue:Float = 0;
		if (hueRad != 0)
		{
			hue = 180 / Math.PI * hueRad;
		}

		return hue < 0 ? hue + 360 : hue;
	}

	inline function get_brightness():Float
	{
		return maxColor();
	}

	inline function get_saturation():Float
	{
		return (maxColor() - minColor()) / brightness;
	}

	inline function get_lightness():Float
	{
		return (maxColor() + minColor()) / 2;
	}

	inline function set_hue(Value:Float):Float
	{
		setHSB(Value, saturation, brightness, alphaFloat);
		return Value;
	}

	inline function set_saturation(Value:Float):Float
	{
		setHSB(hue, Value, brightness, alphaFloat);
		return Value;
	}

	inline function set_brightness(Value:Float):Float
	{
		setHSB(hue, saturation, Value, alphaFloat);
		return Value;
	}

	inline function set_lightness(Value:Float):Float
	{
		setHSL(hue, saturation, Value, alphaFloat);
		return Value;
	}

	inline function set_rgb(value:ProxyFlxColor):ProxyFlxColor
	{
		this._color = (this._color & 0xff000000) | (value._color & 0x00ffffff);
		return value;
	}

	inline function get_rgb():ProxyFlxColor
	{
		return new ProxyFlxColor(this._color & 0x00ffffff);
	}

	inline function maxColor():Float
	{
		return Math.max(redFloat, Math.max(greenFloat, blueFloat));
	}

	inline function minColor():Float
	{
		return Math.min(redFloat, Math.min(greenFloat, blueFloat));
	}

	inline function boundChannel(Value:Int):Int
	{
		return Value > 0xff ? 0xff : Value < 0 ? 0 : Value;
	}

	//@:op(A * B)
	public static inline function multiply(lhs:ProxyFlxColor, rhs:ProxyFlxColor):ProxyFlxColor
	{
		return ProxyFlxColor.fromRGBFloat(lhs.redFloat * rhs.redFloat, lhs.greenFloat * rhs.greenFloat, lhs.blueFloat * rhs.blueFloat);
	}

	//@:op(A + B)
	public static inline function add(lhs:ProxyFlxColor, rhs:ProxyFlxColor):ProxyFlxColor
	{
		return ProxyFlxColor.fromRGB(lhs.red + rhs.red, lhs.green + rhs.green, lhs.blue + rhs.blue);
	}

	//@:op(A - B)
	public static inline function subtract(lhs:ProxyFlxColor, rhs:ProxyFlxColor):ProxyFlxColor
	{
		return ProxyFlxColor.fromRGB(lhs.red - rhs.red, lhs.green - rhs.green, lhs.blue - rhs.blue);
	}
}