package mobile.objects;

#if TOUCH_CONTROLS
import codename.funkin.options.Options;
import flixel.FlxCamera;
import flixel.FlxG;
import flixel.FlxSprite;
import flixel.input.FlxInput;
import flixel.input.IFlxInput;
import flixel.input.touch.FlxTouch;
import flixel.math.FlxMath;
import flixel.math.FlxPoint;
import flixel.sound.FlxSound;
import flixel.system.FlxAssets.FlxShader;
import flixel.util.FlxColor;
import flixel.util.FlxDestroyUtil;
import flixel.util.FlxDestroyUtil.IFlxDestroyable;
import mobile.funkin.backend.system.input.MobileInputID;

/** Public Codename-compatible button with one or more logical input IDs. */
class TouchButton extends TypedTouchButton<FlxSprite>
{
	public static inline var NORMAL:Int = 0;
	public static inline var HIGHLIGHT:Int = 1;
	public static inline var PRESSED:Int = 2;

	public var tag:String;
	public var IDs:Array<MobileInputID> = [];
	public var bounds:FlxSprite = new FlxSprite();

	public function new(X:Float = 0, Y:Float = 0, ?IDs:Array<MobileInputID>):Void
	{
		super(X, Y);
		this.IDs = IDs == null ? [] : IDs.copy();
	}

	public inline function centerInBounds():Void
		setPosition(bounds.x + (bounds.width - frameWidth) * 0.5, bounds.y + (bounds.height - frameHeight) * 0.5);

	public inline function centerBounds():Void
		bounds.setPosition(x + (frameWidth - bounds.width) * 0.5, y + (frameHeight - bounds.height) * 0.5);

	override public function destroy():Void
	{
		bounds = FlxDestroyUtil.destroy(bounds);
		IDs = null;
		super.destroy();
	}
}

#if !display
@:generic
#end
class TypedTouchButton<T:FlxSprite> extends FlxSprite implements IFlxInput
{
	public var label(default, set):T;
	public var labelOffsets:Array<FlxPoint> = [FlxPoint.get(), FlxPoint.get(), FlxPoint.get(0, 1)];
	public var labelAlphas:Array<Float> = [0.8, 1.0, 0.5];
	public var statusAnimations:Array<String> = ['normal', 'highlight', 'pressed'];
	public var allowSwiping:Bool = true;
	public var multiTouch:Bool = false;
	public var maxInputMovement:Float = Math.POSITIVE_INFINITY;

	public var onUp(default, null):TouchButtonEvent;
	public var onDown(default, null):TouchButtonEvent;
	public var onOver(default, null):TouchButtonEvent;
	public var onOut(default, null):TouchButtonEvent;

	public var status(default, set):Int = TouchButton.NORMAL;
	public var statusAlphas:Array<Float> = [1.0, 1.0, 0.6];
	public var statusBrightness:Array<Float> = [1.0, 0.95, 0.7];
	public var labelStatusDiff:Float = 0.05;
	public var parentAlpha(default, set):Float = 1;
	public var statusIndicatorType(default, set):StatusIndicators = ALPHA;
	public var brightShader:ButtonBrightnessShader = new ButtonBrightnessShader();

	public var justReleased(get, never):Bool;
	public var released(get, never):Bool;
	public var pressed(get, never):Bool;
	public var justPressed(get, never):Bool;

	public var deadZones:Array<FlxSprite> = [];
	public var canChangeLabelAlpha:Bool = true;

	var spriteLabel:FlxSprite;
	var input:FlxInput<Int>;
	var ownerTouchID:Int = -1;
	var rejectTouchesUntilClear:Bool = false;
	var pointerWasInside:Bool = false;

	public function new(X:Float = 0, Y:Float = 0):Void
	{
		super(X, Y);
		loadDefaultGraphic();

		onUp = new TouchButtonEvent();
		onDown = new TouchButtonEvent();
		onOver = new TouchButtonEvent();
		onOut = new TouchButtonEvent();
		input = new FlxInput(0);
		scrollFactor.set();

		statusAnimations[TouchButton.HIGHLIGHT] = 'normal';
		labelAlphas[TouchButton.HIGHLIGHT] = 1;
		status = TouchButton.NORMAL;
		rejectTouchesUntilClear = hasHeldTouch();
	}

	/** Ignore all fingers that were already held when this control layer appeared. */
	public function blockUntilTouchesReleased():Void
	{
		ownerTouchID = -1;
		pointerWasInside = false;
		rejectTouchesUntilClear = hasHeldTouch();
		if (input != null) input.reset();
		status = TouchButton.NORMAL;
	}

	override function set_active(Value:Bool):Bool
	{
		if (active != Value && input != null)
		{
			ownerTouchID = -1;
			pointerWasInside = false;
			input.reset();
			status = TouchButton.NORMAL;
			rejectTouchesUntilClear = Value && hasHeldTouch();
		}
		return super.set_active(Value);
	}

	override public function graphicLoaded():Void
	{
		super.graphicLoaded();
		registerFrame('normal', TouchButton.NORMAL);
		registerFrame('pressed', TouchButton.PRESSED);
		refreshAnimation();
	}

	function loadDefaultGraphic():Void
		loadGraphic('flixel/images/ui/button.png', true, 80, 20);

	function registerFrame(name:String, requestedFrame:Int):Void
	{
		var frameCount = #if (flixel < "5.3.0") animation.frames #else animation.numFrames #end;
		if (frameCount > 0)
			animation.add(name, [Std.int(Math.min(requestedFrame, frameCount - 1))]);
	}

	override public function update(elapsed:Float):Void
	{
		super.update(elapsed);

		if (!visible || !active)
		{
			clearLogicalInput();
		}
		else
		{
			updateTouchOwnership();
		}

		if (input != null) input.handleInput();
	}

	/**
	 * One touch owns a button until it leaves or releases. A multi-touch button
	 * may transfer ownership to another finger already inside without producing
	 * a false release between the two fingers.
	 */
	function updateTouchOwnership():Void
	{
		if (rejectTouchesUntilClear)
		{
			clearLogicalInput();
			if (!hasHeldTouch()) rejectTouchesUntilClear = false;
			return;
		}

		var owner = findTouch(ownerTouchID);
		if (owner != null)
		{
			var ownerInside = touchInside(owner);
			if (owner.pressed && ownerInside)
			{
				keepPressed();
				return;
			}

			if (multiTouch)
			{
				var replacement = findCandidate(ownerTouchID, true);
				if (replacement != null)
				{
					ownerTouchID = replacement.touchPointID;
					keepPressed();
					return;
				}
			}

			releaseOwner(owner.justReleased && ownerInside);
			return;
		}

		var candidate = findCandidate(-1, false);
		if (candidate != null)
		{
			if (!pointerWasInside)
			{
				pointerWasInside = true;
				status = TouchButton.HIGHLIGHT;
				onOver.fire();
				if (input == null) return;
			}
			ownerTouchID = candidate.touchPointID;
			input.press();
			status = TouchButton.PRESSED;
			onDown.fire();
			return;
		}

		if (pointerWasInside)
		{
			pointerWasInside = false;
			onOut.fire();
		}
		clearLogicalInput();
	}

	function keepPressed():Void
	{
		pointerWasInside = true;
		input.press();
		status = TouchButton.PRESSED;
	}

	function releaseOwner(releasedInside:Bool):Void
	{
		var hadPress = input.pressed;
		ownerTouchID = -1;
		pointerWasInside = false;
		input.release();
		status = TouchButton.NORMAL;

		if (!hadPress) return;
		if (releasedInside)
			onUp.fire();
		else
			onOut.fire();
	}

	function clearLogicalInput():Void
	{
		ownerTouchID = -1;
		pointerWasInside = false;
		if (input != null) input.release();
		status = TouchButton.NORMAL;
	}

	function findTouch(id:Int):FlxTouch
	{
		if (id < 0) return null;
		for (touch in FlxG.touches.list)
			if (touch != null && touch.touchPointID == id)
				return touch;
		return null;
	}

	function findCandidate(excludedID:Int, transfer:Bool):FlxTouch
	{
		for (touch in FlxG.touches.list)
		{
			if (touch == null || touch.touchPointID == excludedID || !touch.pressed) continue;
			if (!transfer && !touch.justPressed && !allowSwiping) continue;
			if (touchInside(touch)) return touch;
		}
		return null;
	}

	function hasHeldTouch():Bool
	{
		for (touch in FlxG.touches.list)
			if (touch != null && touch.pressed)
				return true;
		return false;
	}

	function touchInside(touch:FlxTouch):Bool
	{
		for (camera in cameras)
		{
			if (maxInputMovement != Math.POSITIVE_INFINITY)
			{
				var viewPosition = touch.getViewPosition(camera, FlxPoint.weak());
				if (touch.justPressedPosition.distanceTo(viewPosition) > maxInputMovement) continue;
			}

			var worldPosition = touch.getWorldPosition(camera, _point);
			if (!overlapsPoint(worldPosition, true, camera) || blockedByDeadZone(worldPosition, camera)) continue;
			return true;
		}
		return false;
	}

	function blockedByDeadZone(point:FlxPoint, camera:FlxCamera):Bool
	{
		for (zone in deadZones)
			if (zone != null && zone.exists && zone.active && zone.visible && zone.overlapsPoint(point, true, camera))
				return true;
		return false;
	}

	function set_label(Value:T):T
	{
		if (Value != null)
			Value.scrollFactor.set(scrollFactor.x, scrollFactor.y);

		label = Value;
		spriteLabel = Value;
		if (spriteLabel != null && statusIndicatorType == BRIGHTNESS) spriteLabel.shader = brightShader;
		updateLabelScale();
		updateLabelPosition();
		refreshLabelAppearance();
		return Value;
	}

	function set_status(Value:Int):Int
	{
		var nextStatus = Std.int(FlxMath.bound(Value, TouchButton.NORMAL, TouchButton.PRESSED));
		if (status == nextStatus) return status;
		status = nextStatus;
		refreshAnimation();
		applyIndicator();
		updateLabelPosition();
		return status;
	}

	function refreshAnimation():Void
	{
		if (animation == null || statusAnimations == null || status < 0 || status >= statusAnimations.length) return;
		var name = statusAnimations[status];
		if (name != null && animation.getByName(name) != null) animation.play(name);
	}

	function applyIndicator():Void
	{
		if (brightShader == null) return;
		switch (statusIndicatorType)
		{
			case ALPHA:
				if (statusAlphas != null && status < statusAlphas.length) super.set_alpha(FlxMath.bound(statusAlphas[status], 0, 1));
			case BRIGHTNESS:
				super.set_alpha(FlxMath.bound(parentAlpha, 0, 1));
				if (statusBrightness != null && status < statusBrightness.length)
					brightShader.brightness.value = [statusBrightness[status]];
			case NONE:
		}
		refreshLabelAppearance();
	}

	function updateLabelPosition():Void
	{
		if (spriteLabel == null) return;
		if (Options.oldPadTexture && labelOffsets != null && status < labelOffsets.length)
		{
			var baseX = pixelPerfectPosition ? Math.floor(x) : x;
			var baseY = pixelPerfectPosition ? Math.floor(y) : y;
			spriteLabel.setPosition(baseX + labelOffsets[status].x, baseY + labelOffsets[status].y);
		}
		else
		{
			spriteLabel.setPosition(x + (width - spriteLabel.width) * 0.5, y + (height - spriteLabel.height) * 0.5);
		}
	}

	function updateLabelScale():Void
	{
		if (spriteLabel != null) spriteLabel.scale.set(scale.x, scale.y);
	}

	function refreshLabelAppearance():Void
	{
		if (spriteLabel == null || !canChangeLabelAlpha) return;
		if (Options.oldPadTexture && labelAlphas != null && status < labelAlphas.length)
			spriteLabel.alpha = alpha * labelAlphas[status];
		else
			spriteLabel.alpha = alpha <= 0 ? 0 : Math.min(1, alpha + labelStatusDiff);
	}

	override public function draw():Void
	{
		super.draw();
		if (spriteLabel != null && spriteLabel.graphic != null && spriteLabel.pixels != null && spriteLabel.visible)
		{
			if (spriteLabel.cameras != cameras) spriteLabel.cameras = cameras;
			spriteLabel.draw();
		}
	}

	#if FLX_DEBUG
	override public function drawDebug():Void
	{
		super.drawDebug();
		if (spriteLabel != null) spriteLabel.drawDebug();
	}
	#end

	override function set_alpha(Value:Float):Float
	{
		var result = super.set_alpha(Value);
		refreshLabelAppearance();
		return result;
	}

	override function set_visible(Value:Bool):Bool
	{
		if (visible != Value && input != null)
		{
			ownerTouchID = -1;
			input.reset();
			if (Value) rejectTouchesUntilClear = hasHeldTouch();
			status = TouchButton.NORMAL;
		}
		var result = super.set_visible(Value);
		if (spriteLabel != null) spriteLabel.visible = Value;
		return result;
	}

	override function set_x(Value:Float):Float
	{
		var result = super.set_x(Value);
		updateLabelPosition();
		return result;
	}

	override function set_y(Value:Float):Float
	{
		var result = super.set_y(Value);
		updateLabelPosition();
		return result;
	}

	override function set_color(Value:FlxColor):Int
	{
		if (spriteLabel != null) spriteLabel.color = Value;
		if (brightShader != null) brightShader.color = Value;
		return super.set_color(Value);
	}

	override private function set_width(Value:Float):Float
	{
		var result = super.set_width(Value);
		updateLabelScale();
		updateLabelPosition();
		return result;
	}

	override private function set_height(Value:Float):Float
	{
		var result = super.set_height(Value);
		updateLabelScale();
		updateLabelPosition();
		return result;
	}

	override public function updateHitbox():Void
	{
		super.updateHitbox();
		if (spriteLabel != null)
		{
			updateLabelScale();
			spriteLabel.updateHitbox();
			updateLabelPosition();
		}
	}

	function set_parentAlpha(Value:Float):Float
	{
		parentAlpha = FlxMath.bound(Value, 0, 1);
		statusAlphas = [
			parentAlpha,
			Math.max(0, parentAlpha - 0.05),
			parentAlpha > 0 ? Math.max(0.25, parentAlpha - 0.45) : 0
		];
		applyIndicator();
		return parentAlpha;
	}

	function set_statusIndicatorType(Value:StatusIndicators):StatusIndicators
	{
		statusIndicatorType = Value;
		var selectedShader = Value == BRIGHTNESS ? brightShader : null;
		shader = selectedShader;
		if (spriteLabel != null) spriteLabel.shader = selectedShader;
		applyIndicator();
		return Value;
	}

	inline function get_justReleased():Bool return input != null && input.justReleased;
	inline function get_released():Bool return input == null || input.released;
	inline function get_pressed():Bool return input != null && input.pressed;
	inline function get_justPressed():Bool return input != null && input.justPressed;

	override public function destroy():Void
	{
		label = FlxDestroyUtil.destroy(label);
		spriteLabel = null;
		onUp = FlxDestroyUtil.destroy(onUp);
		onDown = FlxDestroyUtil.destroy(onDown);
		onOver = FlxDestroyUtil.destroy(onOver);
		onOut = FlxDestroyUtil.destroy(onOut);
		deadZones = FlxDestroyUtil.destroyArray(deadZones);
		labelOffsets = FlxDestroyUtil.putArray(labelOffsets);
		labelAlphas = null;
		statusAlphas = null;
		statusBrightness = null;
		input = null;
		brightShader = null;
		super.destroy();
	}
}

private class TouchButtonEvent implements IFlxDestroyable
{
	public var callback:Void->Void;

	#if FLX_SOUND_SYSTEM
	public var sound:FlxSound;
	#end

	public function new(?callback:Void->Void, ?sound:FlxSound):Void
	{
		this.callback = callback;
		#if FLX_SOUND_SYSTEM
		this.sound = sound;
		#end
	}

	public function destroy():Void
	{
		callback = null;
		#if FLX_SOUND_SYSTEM
		sound = FlxDestroyUtil.destroy(sound);
		#end
	}

	public inline function fire():Void
	{
		if (callback != null) callback();
		#if FLX_SOUND_SYSTEM
		if (sound != null) sound.play(true);
		#end
	}
}

class ButtonBrightnessShader extends FlxShader
{
	public var color(default, set):Null<FlxColor> = FlxColor.WHITE;

	@:glFragmentSource('
		#pragma header
		uniform float brightness;
		void main()
		{
			vec4 pixel = flixel_texture2D(bitmap, openfl_TextureCoordv);
			pixel.rgb *= brightness;
			gl_FragColor = pixel;
		}
	')
	public function new()
	{
		super();
		brightness.value = [1.0];
	}

	function set_color(value:Null<FlxColor>):Null<FlxColor>
	{
		if (value == null)
		{
			colorMultiplier.value = [1, 1, 1, 1];
			hasColorTransform.value = hasTransform.value = [false];
		}
		else
		{
			hasColorTransform.value = hasTransform.value = [true];
			colorMultiplier.value = [value.redFloat, value.greenFloat, value.blueFloat, value.alphaFloat];
		}
		return color = value;
	}
}

enum StatusIndicators
{
	ALPHA;
	BRIGHTNESS;
	NONE;
}
#end
