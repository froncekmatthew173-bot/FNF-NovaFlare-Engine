package mobile.objects;

#if TOUCH_CONTROLS
import codename.funkin.backend.assets.Paths;
import codename.funkin.options.Options;
import flixel.FlxG;
import flixel.FlxSprite;
import flixel.graphics.FlxGraphic;
import flixel.graphics.frames.FlxTileFrames;
import flixel.math.FlxPoint;
import flixel.util.FlxColor;
import haxe.io.Path;
import mobile.funkin.backend.system.input.MobileInputID;
import mobile.funkin.backend.system.input.MobileInputManager;
import mobile.funkin.backend.utils.MobileData;
import mobile.funkin.backend.utils.MobileLayout;
import mobile.funkin.backend.utils.MobileLayout.MobileButtonSlot;
import mobile.objects.TouchButton.StatusIndicators;
import openfl.utils.Assets;

/** Typed, code-laid-out virtual pad used by the Codename state chain. */
@:access(mobile.objects.TouchButton)
class TouchPad extends MobileInputManager
{
	public var buttonLeft:TouchButton;
	public var buttonUp:TouchButton;
	public var buttonRight:TouchButton;
	public var buttonDown:TouchButton;
	public var buttonLeft2:TouchButton;
	public var buttonUp2:TouchButton;
	public var buttonRight2:TouchButton;
	public var buttonDown2:TouchButton;
	public var buttonA:TouchButton;
	public var buttonB:TouchButton;
	public var buttonC:TouchButton;
	public var buttonD:TouchButton;
	public var buttonE:TouchButton;
	public var buttonF:TouchButton;
	public var buttonG:TouchButton;
	public var buttonH:TouchButton;
	public var buttonI:TouchButton;
	public var buttonJ:TouchButton;
	public var buttonK:TouchButton;
	public var buttonL:TouchButton;
	public var buttonM:TouchButton;
	public var buttonN:TouchButton;
	public var buttonO:TouchButton;
	public var buttonP:TouchButton;
	public var buttonQ:TouchButton;
	public var buttonR:TouchButton;
	public var buttonS:TouchButton;
	public var buttonT:TouchButton;
	public var buttonU:TouchButton;
	public var buttonV:TouchButton;
	public var buttonW:TouchButton;
	public var buttonX:TouchButton;
	public var buttonY:TouchButton;
	public var buttonZ:TouchButton;

	public var instance:MobileInputManager;
	public var curDPadMode(default, null):String = 'NONE';
	public var curActionMode(default, null):String = 'NONE';

	public function new(DPad:String, Action:String)
	{
		super();
		if (MobileData.dpadModes.keys().hasNext() == false || MobileData.actionModes.keys().hasNext() == false)
			MobileData.init();

		curDPadMode = MobileData.normalize(DPad);
		curActionMode = MobileData.normalize(Action);
		installMode(curDPadMode, MobileData.dpadModes, 'dpad');
		installMode(curActionMode, MobileData.actionModes, 'action');

		scrollFactor.set();
		updateTrackedButtons();
		alpha = Options.touchPadAlpha;
		instance = this;
	}

	function installMode(id:String, registry:Map<String, MobileLayout>, kind:String):Void
	{
		if (id == 'NONE') return;
		var layout = registry.get(id);
		if (layout == null) throw 'Unknown TouchPad $kind mode "$id".';
		for (slot in layout.slots) installSlot(slot);
	}

	function installSlot(slot:MobileButtonSlot):Void
	{
		var button = createButton(slot.resolveX(FlxG.width), slot.resolveY(FlxG.height), slot.symbol, slot.tint, [slot.input]);
		bindPublicField(slot.input, button);
		add(button);
	}

	function bindPublicField(id:MobileInputID, button:TouchButton):Void
	{
		switch (id)
		{
			case MobileInputID.LEFT: buttonLeft = button;
			case MobileInputID.UP: buttonUp = button;
			case MobileInputID.RIGHT: buttonRight = button;
			case MobileInputID.DOWN: buttonDown = button;
			case MobileInputID.LEFT2: buttonLeft2 = button;
			case MobileInputID.UP2: buttonUp2 = button;
			case MobileInputID.RIGHT2: buttonRight2 = button;
			case MobileInputID.DOWN2: buttonDown2 = button;
			case MobileInputID.A: buttonA = button;
			case MobileInputID.B: buttonB = button;
			case MobileInputID.C: buttonC = button;
			case MobileInputID.D: buttonD = button;
			case MobileInputID.E: buttonE = button;
			case MobileInputID.F: buttonF = button;
			case MobileInputID.G: buttonG = button;
			case MobileInputID.H: buttonH = button;
			case MobileInputID.I: buttonI = button;
			case MobileInputID.J: buttonJ = button;
			case MobileInputID.K: buttonK = button;
			case MobileInputID.L: buttonL = button;
			case MobileInputID.M: buttonM = button;
			case MobileInputID.N: buttonN = button;
			case MobileInputID.O: buttonO = button;
			case MobileInputID.P: buttonP = button;
			case MobileInputID.Q: buttonQ = button;
			case MobileInputID.R: buttonR = button;
			case MobileInputID.S: buttonS = button;
			case MobileInputID.T: buttonT = button;
			case MobileInputID.U: buttonU = button;
			case MobileInputID.V: buttonV = button;
			case MobileInputID.W: buttonW = button;
			case MobileInputID.X: buttonX = button;
			case MobileInputID.Y: buttonY = button;
			case MobileInputID.Z: buttonZ = button;
			default: throw 'Input $id cannot be placed on a TouchPad.';
		}
	}

	function createButton(X:Float, Y:Float, symbol:String, tint:FlxColor, IDs:Array<MobileInputID>):TouchButton
	{
		var button = new TouchButton(X, Y, IDs);
		if (Options.oldPadTexture)
		{
			var image = resolveMobileAsset([
				'images/virtualpad/${symbol.toLowerCase()}.png',
				'images/virtualpad/${symbol.toUpperCase()}.png',
				'images/virtualpad/default.png'
			]);
			var graphic = FlxGraphic.fromBitmapData(Assets.getBitmapData(image));
			button.frames = FlxTileFrames.fromGraphic(graphic, FlxPoint.get(Std.int(graphic.width * 0.5), graphic.height));
			button.statusBrightness = [1, 0.8, 0.4];
			button.statusIndicatorType = StatusIndicators.BRIGHTNESS;
		}
		else
		{
			button.loadGraphic(Assets.getBitmapData(resolveMobileAsset(['images/touchpad/bg.png'])));
			button.label = new FlxSprite().loadGraphic(Assets.getBitmapData(resolveMobileAsset([
				'images/touchpad/${symbol.toUpperCase()}.png',
				'images/touchpad/${symbol.toLowerCase()}.png'
			])));
			button.scale.set(0.243, 0.243);
		}

		button.labelStatusDiff = 0.05;
		if (button.label != null) button.label.antialiasing = Options.antialiasing;
		button.antialiasing = Options.antialiasing;
		button.color = tint;
		button.updateHitbox();
		button.updateLabelPosition();

		button.bounds.makeGraphic(
			Std.int(Math.max(1, button.width - 50)),
			Std.int(Math.max(1, button.height - 50)),
			FlxColor.TRANSPARENT);
		button.centerBounds();
		button.immovable = true;
		button.solid = false;
		button.moves = false;
		button.tag = symbol.toUpperCase();
		button.parentAlpha = button.alpha;
		return button;
	}

	/** Resolve a case-insensitive asset name through the active CNE library tree. */
	static function resolveMobileAsset(candidates:Array<String>):String
	{
		for (candidate in candidates)
		{
			var logical = 'assets/mobile/$candidate';
			var folder = Path.directory(logical);
			var requestedName = Path.withoutDirectory(logical).toLowerCase();
			for (entry in Paths.assetsTree.getFiles(folder))
			{
				if (entry.toLowerCase() != requestedName) continue;
				var resolved = '$folder/$entry';
				if (Assets.exists(resolved)) return resolved;
			}

			var fallback = Paths.getPath('mobile/$candidate');
			if (Assets.exists(fallback)) return fallback;
		}
		throw 'Missing Codename mobile image: ${candidates.join(", ")}';
	}

	override function set_alpha(Value:Float):Float
	{
		var result = super.set_alpha(Value);
		forEachAlive((button:TouchButton) -> button.parentAlpha = Value);
		return result;
	}

	override public function destroy():Void
	{
		instance = null;
		super.destroy();
		clearPublicFields();
	}

	function clearPublicFields():Void
	{
		buttonLeft = buttonUp = buttonRight = buttonDown = null;
		buttonLeft2 = buttonUp2 = buttonRight2 = buttonDown2 = null;
		buttonA = buttonB = buttonC = buttonD = buttonE = buttonF = buttonG = null;
		buttonH = buttonI = buttonJ = buttonK = buttonL = buttonM = buttonN = null;
		buttonO = buttonP = buttonQ = buttonR = buttonS = buttonT = buttonU = null;
		buttonV = buttonW = buttonX = buttonY = buttonZ = null;
	}
}
#end
