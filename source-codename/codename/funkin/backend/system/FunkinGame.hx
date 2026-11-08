package codename.funkin.backend.system;

import flixel.FlxGame;
import flixel.FlxState;

class FunkinGame extends FlxGame {
	/**
	 * Compatibility bridge for Codename mods written against its older Flixel
	 * fork. That fork exposed the pending state instance as `_requestedState`,
	 * while NovaFlare's current Flixel stores a `NextState` in `_nextState`.
	 */
	public var _requestedState(get, set):FlxState;

	private function get__requestedState():FlxState {
		@:privateAccess
		return _nextState == null ? null : _nextState.createInstance();
	}

	private function set__requestedState(value:FlxState):FlxState {
		@:privateAccess
		_nextState = value;
		return value;
	}

	var skipNextTickUpdate:Bool = false;
	public override function switchState() {
		super.switchState();
		// draw once to put all images in gpu then put the last update time to now to prevent lag spikes or whatever
		draw();
		_total = ticks = getTicks();
		skipNextTickUpdate = true;
	}

	public override function onEnterFrame(t) {
		if (skipNextTickUpdate != (skipNextTickUpdate = false))
			_total = ticks = getTicks();
		super.onEnterFrame(t);
	}
}
