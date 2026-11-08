package codename.funkin.backend;

import flixel.FlxState;
import flixel.tweens.FlxTween;
import codename.funkin.backend.scripting.Script;
import codename.funkin.backend.scripting.events.CancellableEvent;
import codename.funkin.backend.scripting.events.ResizeEvent;
import codename.funkin.backend.scripting.events.TransitionCreationEvent;
import codename.funkin.backend.utils.FunkinParentDisabler;
import codename.funkin.editors.ui.UIState;

class MusicBeatTransition extends MusicBeatSubstate {
	public static var script:String = Flags.DEFAULT_TRANSITION_SCRIPT;
	public var transitionScript:Script;

	var nextFrameSkip:Bool = false;

	public var transitionTween:FlxTween = null;
	public var transitionCamera:FlxCamera;
	public var newState:FlxState;
	public var transOut:Bool = false;

	public var allowSkip:Bool = true;

	public var blackSpr:FlxSprite;
	public var transitionSprite:FunkinSprite;
	public function new(?newState:FlxState) {
		super();
		this.newState = newState;
	}

	public override function create() {
		if (newState != null)
			add(new FunkinParentDisabler(true, false));

		transitionCamera = new FlxCamera();
		transitionCamera.bgColor = 0;
		FlxG.cameras.add(transitionCamera, false);

		cameras = [transitionCamera];

		transitionScript = Script.create(Paths.script(script));
		transitionScript.setParent(this);
		transitionScript.load();

		var event = EventManager.get(TransitionCreationEvent).recycle(newState != null, newState);
		transitionScript.call('create', [event]);

		transOut = event.transOut;
		newState = event.newState;

		if (event.cancelled) {
			super.create();
			return;
		}

		add(blackSpr = new FlxSprite(0, transOut ? -transitionCamera.height : transitionCamera.height).makeGraphic(1, 1, -1));
		blackSpr.color = 0xFF000000;
		add(transitionSprite = new FunkinSprite().loadSprite(Paths.image('menus/transitionSpr')));
		resizeDefaultSprites();

		// CNE's original Flixel fork flips this texture through the camera. NF's
		// renderer has a different camera pipeline, so flip the gradient itself to
		// keep the transparent edge leading the incoming transition.
		transitionCamera.flipY = false;
		transitionSprite.flipY = !transOut;
		transitionCamera.scroll.y = transitionCamera.height;
		transitionTween = FlxTween.tween(transitionCamera.scroll, {y: -transitionCamera.height}, 2/3, {
			ease: FlxEase.sineOut,
			onComplete: function(_) {
				finish();
			}
		});

		super.create();
		transitionScript.call('postCreate', [event]);
	}

	public override function update(elapsed:Float) {
		transitionScript.call('update', [elapsed]);
		super.update(elapsed);

		if (nextFrameSkip) {
			var event = new CancellableEvent();
			transitionScript.call('onSkip', [event]);
			if (!event.cancelled) {
				finish();
				return;
			}
		}

		if (allowSkip && !parent.persistentUpdate && FlxG.keys.pressed.SHIFT) {
			// skip
			if (newState != null) {
				nextFrameSkip = true;
				parent.persistentDraw = false;
			} else {
				var event = new CancellableEvent();
				transitionScript.call('onSkip', [event]);
				if (!event.cancelled) {
					finish();
				}
			}
		}
		transitionScript.call('postUpdate', [elapsed]);
	}

	public override function onResize(w:Int, h:Int) {
		super.onResize(w, h);
		if (FlxG.state is UIState && UIState.resolutionAware) resizeDefaultSprites();
		transitionScript.call('onResize', [EventManager.get(ResizeEvent).recycle(w, h, null, null)]);
	}

	public function resizeDefaultSprites() {
		if (blackSpr != null) {
			blackSpr.scale.set(transitionCamera.width, transitionCamera.height);
			blackSpr.updateHitbox();
		}

		if (transitionSprite != null) {
			transitionSprite.setGraphicSize(transitionCamera.width, transitionCamera.height);
			transitionSprite.updateHitbox();
		}
	}

	public function finish() {
		var event = new CancellableEvent();
		transitionScript.call('onFinish', [event]);
		if (event.cancelled) return;

		if (newState != null)
			FlxG.switchState(newState);
		close();

		transitionScript.call('onPostFinish');
	}

	public override function destroy() {
		transitionScript.call('destroy');

		if (transitionTween != null) transitionTween.cancel();
		transitionTween = FlxDestroyUtil.destroy(transitionTween);
		if (newState == null && FlxG.cameras.list.contains(transitionCamera)) FlxG.cameras.remove(transitionCamera);
		else transitionCamera.bgColor = 0xFF000000;

		transitionScript.destroy();
		super.destroy();
	}
}
