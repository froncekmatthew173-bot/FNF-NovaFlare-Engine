package codename.funkin.editors.ui;

import openfl.display.ShaderParameter;
import openfl.display.ShaderInput;
import openfl.filters.ShaderFilter;
import flixel.tweens.FlxEase;
import flixel.tweens.FlxTween;
import codename.funkin.backend.shaders.CustomShader;

class UIWarningSubstate extends MusicBeatSubstate {
	var cameraBlurFilters:Array<{camera:FlxCamera, filter:ShaderFilter}> = [];
	var cameraBlurRemoved:Bool = false;
	var blurShader:CustomShader = {
		var _ = new CustomShader(Options.intensiveBlur ? "engine/editorBlur" : "engine/editorBlurFast");
		if(!Options.intensiveBlur) {
			var noiseTexture:ShaderInput<openfl.display.BitmapData> = _.data.noiseTexture;
			noiseTexture.input = Assets.getBitmapData("assets/shaders/noise256.png");
			noiseTexture.wrap = REPEAT;
			var noiseTextureSize:ShaderParameter<Float> = _.data.noiseTextureSize;
			noiseTextureSize.value = [noiseTexture.input.width, noiseTexture.input.height];
		}
		_;
	};

	public var bHeight:Int = 232;

	var title:String;
	var message:String;
	var buttons:Array<WarningButton>;
	var isError:Bool = true;
	var backButtonIndex:Int = 0;

	var titleSpr:UIText;
	var messageSpr:UIText;

	var warnCam:FlxCamera;

	public override function onSubstateOpen() {
		super.onSubstateOpen();
		parent.persistentUpdate = false;
		parent.persistentDraw = true;
	}

	public override function create() {
		for(c in FlxG.cameras.list) {
			// Prevent adding a shader if it already has one
			if(c.filters != null) {
				var shouldSkip = false;
				for(filter in c.filters) {
					if(filter is ShaderFilter) {
						var filter:ShaderFilter = cast filter;
						if(filter.shader is CustomShader) {
							var shader:CustomShader = cast filter.shader;

							if(shader.path == blurShader.path) {
								shouldSkip = true;
								break;
							}
						}
					}
				}
				if(shouldSkip)
					continue;
			}

			// Replace the array instead of mutating it in place. FlxCamera caches
			// the last applied array by identity, so push/remove on the same array
			// can leave the OpenFL display object using stale filters.
			var filter = new ShaderFilter(blurShader);
			var filters = c.filters == null ? [] : c.filters.copy();
			filters.push(filter);
			c.filters = filters;
			cameraBlurFilters.push({camera: c, filter: filter});
		}

		camera = warnCam = new FlxCamera();
		warnCam.bgColor = 0;
		warnCam.alpha = 0;
		warnCam.zoom = 0.1;
		FlxG.cameras.add(warnCam, false);


		var spr = new UISliceSprite(0, 0, CoolUtil.maxInt(560, 30 + (170 * buttons.length)), bHeight, 'editors/ui/${isError ? "normal" : "grayscale"}-popup');

		var sprIcon:FlxSprite = new FlxSprite(spr.x + 18, spr.y + 28 + 26).loadGraphic(Paths.image('editors/warnings/${isError ? "error" : "warning"}'));
		sprIcon.scale.set(1.4, 1.4);
		sprIcon.updateHitbox();

		messageSpr = new UIText(0,0, spr.bWidth - 100 - (26 * 2), message);
		spr.bHeight = Std.int(bHeight + Math.abs(Math.min(sprIcon.height-messageSpr.height, 0)));

		spr.x = (FlxG.width - spr.bWidth) / 2;
		spr.y = (FlxG.height - spr.bHeight) / 2;
		spr.color = isError ? 0xFFFF0000 : 0xFFFFFF00;
		add(spr);

		if(title != null) {
			add(titleSpr = new UIText(spr.x + 25, spr.y, spr.bWidth - 50, title, 15, -1));
			titleSpr.y = spr.y + ((30 - titleSpr.height) / 2);
		}

		var sprIcon:FlxSprite = new FlxSprite(spr.x + 18, spr.y + 28 + 26).loadGraphic(Paths.image('editors/warnings/${isError ? "error" : "warning"}'));
		sprIcon.scale.set(1.4, 1.4);
		sprIcon.updateHitbox();
		sprIcon.antialiasing = true;
		add(sprIcon);

		messageSpr.x = sprIcon.x + 70 + 16 + 20;
		messageSpr.y = sprIcon.y;
		add(messageSpr);

		var xPos = (FlxG.width - (30 + (170 * buttons.length))) / 2;
		for(k=>b in buttons) {
			var button = new UIButton(xPos + 20 + (170 * k), spr.y + spr.bHeight - (36 + 16), b.label, function() {
				b.onClick(this);
				close();
			}, 160, 30);
			if (b.color != null) {
				button.frames = Paths.getFrames("editors/ui/grayscale-button");
				button.color = b.color;
			}
			add(button);
		}

		FlxTween.tween(camera, {alpha: 1}, 0.25, {ease: FlxEase.cubeOut});
		FlxTween.tween(camera, {zoom: 1}, 0.66, {ease: FlxEase.elasticOut});

		CoolUtil.playMenuSFX(WARNING);
	}

	public override function update(elapsed:Float) {
		#if mobile
		if (controls.BACK && buttons.length > 0) {
			var index = FlxMath.bound(backButtonIndex, 0, buttons.length - 1);
			buttons[Std.int(index)].onClick(this);
			close();
			return;
		}
		#end
		super.update(elapsed);
	}

	function removeCameraBlur():Void
	{
		if (cameraBlurRemoved) return;
		cameraBlurRemoved = true;

		for (entry in cameraBlurFilters) {
			if (entry == null || entry.camera == null || entry.camera.filters == null) continue;
			var filters = entry.camera.filters.copy();
			filters.remove(entry.filter);
			entry.camera.filters = filters.length > 0 ? filters : null;
		}
		cameraBlurFilters = [];
	}

	public override function close():Void
	{
		// Remove immediately on Cancel/Back. Waiting for the deferred substate
		// destroy leaves at least one rendered frame with the stale filter.
		removeCameraBlur();
		super.close();
	}

	public override function destroy() {
		removeCameraBlur();
		if (warnCam != null) {
			FlxTween.cancelTweensOf(warnCam);
			if (FlxG.cameras.list.contains(warnCam)) FlxG.cameras.remove(warnCam);
		}
		super.destroy();
	}

	public function new(title:String, message:String, buttons:Array<WarningButton>, ?isError:Bool = true, ?backButtonIndex:Int = 0) {
		super();
		this.title = title;
		this.message = message;
		this.buttons = buttons;
		this.isError = isError;
		this.backButtonIndex = backButtonIndex;
	}
}

typedef WarningButton = {
	var label:String;
	var ?color:Int;
	var onClick:UIWarningSubstate->Void;
}
