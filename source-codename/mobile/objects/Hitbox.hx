package mobile.objects;

#if TOUCH_CONTROLS
import codename.funkin.options.Options;
import flixel.FlxG;
import flixel.graphics.FlxGraphic;
import flixel.tweens.FlxEase;
import flixel.tweens.FlxTween;
import flixel.util.FlxSignal.FlxTypedSignal;
import mobile.funkin.backend.system.input.MobileInputID;
import mobile.funkin.backend.system.input.MobileInputManager;
import mobile.objects.TouchButton.StatusIndicators;
import openfl.display.BitmapData;
import openfl.display.GradientType;
import openfl.display.InterpolationMethod;
import openfl.display.Shape;
import openfl.display.SpreadMethod;
import openfl.geom.Matrix;

using StringTools;

/** Four-lane gameplay surface with optional full-width auxiliary zones. */
class Hitbox extends MobileInputManager
{
	public var buttonLeft:TouchButton;
	public var buttonDown:TouchButton;
	public var buttonUp:TouchButton;
	public var buttonRight:TouchButton;
	public var buttonExtra:TouchButton;
	public var buttonExtra2:TouchButton;

	public var instance:MobileInputManager;
	public var onButtonDown:FlxTypedSignal<TouchButton->Void> = new FlxTypedSignal<TouchButton->Void>();
	public var onButtonUp:FlxTypedSignal<TouchButton->Void> = new FlxTypedSignal<TouchButton->Void>();

	var feedbackTweens:Map<TouchButton, FlxTween> = new Map();

	public function new(?extraMode:String = 'NONE')
	{
		super();
		var mode = extraMode == null ? 'NONE' : extraMode.trim().toUpperCase();
		var quarterWidth = Std.int(FlxG.width / 4);
		var quarterHeight = Std.int(FlxG.height / 4);
		var laneTop = mode == 'NONE' ? 0 : (Options.hitboxPos ? 0 : quarterHeight);
		var laneHeight = mode == 'NONE' ? FlxG.height : quarterHeight * 3;

		var slots:Array<HitboxSlot> = [
			new HitboxSlot(MobileInputID.HITBOX_LEFT, 0, laneTop, quarterWidth, laneHeight, 0xFFC24B99),
			new HitboxSlot(MobileInputID.HITBOX_DOWN, quarterWidth, laneTop, quarterWidth, laneHeight, 0xFF00FFFF),
			new HitboxSlot(MobileInputID.HITBOX_UP, quarterWidth * 2, laneTop, quarterWidth, laneHeight, 0xFF12FA05),
			new HitboxSlot(MobileInputID.HITBOX_RIGHT, quarterWidth * 3, laneTop, quarterWidth, laneHeight, 0xFFF9393F)
		];

		var extraTop = Options.hitboxPos ? quarterHeight * 3 : 0;
		switch (mode)
		{
			case 'NONE':
			case 'SINGLE':
				slots.push(new HitboxSlot(MobileInputID.EXTRA_1, 0, extraTop, FlxG.width, quarterHeight, 0xFF0066FF));
			case 'DOUBLE':
				slots.push(new HitboxSlot(MobileInputID.EXTRA_2, quarterWidth * 2, extraTop, quarterWidth * 2, quarterHeight, 0xFFA6FF00));
				slots.push(new HitboxSlot(MobileInputID.EXTRA_1, 0, extraTop, quarterWidth * 2, quarterHeight, 0xFF0066FF));
			default:
				throw 'Unknown hitbox extra mode "$mode".';
		}

		for (slot in slots)
		{
			var button = createHint(slot);
			bindLane(slot.input, button);
			add(button);
		}

		scrollFactor.set();
		updateTrackedButtons();
		instance = this;
	}

	function bindLane(id:MobileInputID, button:TouchButton):Void
	{
		switch (id)
		{
			case MobileInputID.HITBOX_LEFT: buttonLeft = button;
			case MobileInputID.HITBOX_DOWN: buttonDown = button;
			case MobileInputID.HITBOX_UP: buttonUp = button;
			case MobileInputID.HITBOX_RIGHT: buttonRight = button;
			case MobileInputID.EXTRA_1: buttonExtra = button;
			case MobileInputID.EXTRA_2: buttonExtra2 = button;
			default: throw 'Input $id cannot be assigned to a gameplay hitbox.';
		}
	}

	function createHint(slot:HitboxSlot):TouchButton
	{
		var hint = new TouchButton(slot.x, slot.y, [slot.input]);
		hint.statusAlphas = [];
		hint.statusIndicatorType = StatusIndicators.NONE;
		hint.loadGraphic(buildHintGraphic(slot.width, slot.height));
		hint.onDown.callback = () -> pressHint(hint);
		hint.onUp.callback = () -> releaseHint(hint);
		hint.onOut.callback = () -> releaseHint(hint);
		hint.immovable = true;
		hint.multiTouch = true;
		hint.solid = false;
		hint.moves = false;
		hint.alpha = 0.00001;
		hint.canChangeLabelAlpha = true;
		hint.antialiasing = Options.antialiasing;
		hint.color = slot.tint;
		#if FLX_DEBUG
		hint.ignoreDrawDebug = true;
		#end
		return hint;
	}

	function pressHint(button:TouchButton):Void
	{
		onButtonDown.dispatch(button);
		if (Options.hitboxType != 'hidden') tweenFeedback(button, Options.hitboxAlpha, Options.hitboxAlpha / 100);
	}

	function releaseHint(button:TouchButton):Void
	{
		onButtonUp.dispatch(button);
		if (Options.hitboxType != 'hidden') tweenFeedback(button, 0.00001, Options.hitboxAlpha / 10);
	}

	function tweenFeedback(button:TouchButton, targetAlpha:Float, duration:Float):Void
	{
		var previous = feedbackTweens.get(button);
		if (previous != null) previous.cancel();
		var tween = FlxTween.tween(button, {alpha: targetAlpha}, duration, {
			ease: FlxEase.circInOut,
			onComplete: _ -> feedbackTweens.remove(button)
		});
		feedbackTweens.set(button, tween);
	}

	function buildHintGraphic(width:Int, height:Int):FlxGraphic
	{
		var canvas = new Shape();
		var graphics = canvas.graphics;
		switch (Options.hitboxType)
		{
			case 'noGradient':
				var radialMatrix = new Matrix();
				radialMatrix.createGradientBox(width, height);
				graphics.beginGradientFill(GradientType.RADIAL, [0xFFFFFF, 0xFFFFFF], [0, 1], [60, 255],
					radialMatrix, SpreadMethod.PAD, InterpolationMethod.RGB);
				graphics.drawRect(0, 0, width, height);
				graphics.endFill();
			case 'noGradientOld':
				graphics.beginFill(0xFFFFFF);
				graphics.lineStyle(10, 0xFFFFFF, 1);
				graphics.drawRect(0, 0, width, height);
				graphics.endFill();
			default:
				graphics.beginFill(0xFFFFFF);
				graphics.lineStyle(3, 0xFFFFFF, 1);
				graphics.drawRect(0, 0, width, height);
				graphics.lineStyle(0, 0, 0);
				graphics.drawRect(3, 3, width - 6, height - 6);
				graphics.endFill();
				graphics.beginGradientFill(GradientType.RADIAL, [0xFFFFFF, 0x000000], [1, 0], [0, 255],
					null, SpreadMethod.PAD, InterpolationMethod.RGB, 0.5);
				graphics.drawRect(3, 3, width - 6, height - 6);
				graphics.endFill();
		}

		var bitmap = new BitmapData(width, height, true, 0);
		bitmap.draw(canvas);
		return FlxG.bitmap.add(bitmap);
	}

	override public function destroy():Void
	{
		for (tween in feedbackTweens) if (tween != null) tween.cancel();
		feedbackTweens.clear();
		feedbackTweens = null;
		onButtonUp.destroy();
		onButtonDown.destroy();
		onButtonUp = null;
		onButtonDown = null;
		instance = null;
		super.destroy();
		buttonLeft = buttonDown = buttonUp = buttonRight = null;
		buttonExtra = buttonExtra2 = null;
	}
}

private final class HitboxSlot
{
	public final input:MobileInputID;
	public final x:Float;
	public final y:Float;
	public final width:Int;
	public final height:Int;
	public final tint:Int;

	public function new(input:MobileInputID, x:Float, y:Float, width:Int, height:Int, tint:Int)
	{
		this.input = input;
		this.x = x;
		this.y = y;
		this.width = width;
		this.height = height;
		this.tint = tint;
	}
}
#end
