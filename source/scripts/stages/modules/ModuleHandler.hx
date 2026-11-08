package scripts.stages.modules;

class ModuleHandler {
	private static var moduleArray:Array<Module> = [];
	public static var includeExtension:Array<String> = ["hxc", "hxs"];

	public static function init() {
		moduleArray = [];

		for(fp in ScriptedModule.__sc_scriptClassLists()) {
			ModuleAgency.runThrow(function() {
				var instance:Module = ScriptedModule.createScriptClassInstance(fp);
				instance.onCreate();
				moduleArray.push(instance);
			}, "hscript");
		}

		globalHandler();
	}

	static var sbCallbacks:Map<String, haxe.Constraints.Function>;
	private static function globalHandler() {
		if(sbCallbacks != null) {
			for(k=>v in sbCallbacks) {
				switch(k) {
					case "onStateSwitch":
						FlxG.signals.preStateSwitch.remove(cast v);
					case "onStateSwitchPost":
						FlxG.signals.postStateSwitch.remove(cast v);
					case "onStateCreate":
						FlxG.signals.preStateCreate.remove(cast v);
					case "onGameResized":
						FlxG.signals.gameResized.remove(cast v);
					case "onGameReset":
						FlxG.signals.preGameReset.remove(cast v);
					case "onGameResetPost":
						FlxG.signals.postGameReset.remove(cast v);
					case "onGameStart":
						FlxG.signals.preGameStart.remove(cast v);
					case "onGameStartPost":
						FlxG.signals.postGameStart.remove(cast v);
					case "onUpdate":
						FlxG.signals.preUpdate.remove(cast v);
					case "onUpdatePost":
						FlxG.signals.postUpdate.remove(cast v);
					case "onDraw":
						FlxG.signals.preDraw.remove(cast v);
					case "onDrawPost":
						FlxG.signals.postDraw.remove(cast v);
					case "onFocusGained":
						FlxG.signals.focusGained.remove(cast v);
					case "onFocusLost":
						FlxG.signals.focusLost.remove(cast v);
					case _:
				}
			}
		}

		sbCallbacks = new Map();
		for(id in ["onStateSwitch", "onStateSwitchPost", "onStateCreate", "onGameResized", "onGameReset", "onGameResetPost", "onGameStart", "onGameStartPost", "onUpdate", "onUpdatePost", "onDraw", "onDrawPost", "onFocusGained", "onFocusLost"]) {
			switch(id) {
				case "onStateSwitch":
					sbCallbacks.set(id, function() {
						ModuleHandler.call(id);
					});
					FlxG.signals.preStateSwitch.add(cast sbCallbacks.get(id));
				case "onStateSwitchPost":
					sbCallbacks.set(id, function() {
						ModuleHandler.call(id);
					});
					FlxG.signals.postStateSwitch.add(cast sbCallbacks.get(id));
				case "onStateCreate":
					sbCallbacks.set(id, function(state:FlxState) {
						ModuleHandler.call(id, [state]);
					});
					FlxG.signals.preStateCreate.add(cast sbCallbacks.get(id));
				case "onGameResized":
					sbCallbacks.set(id, function(width:Int, height:Int) {
						ModuleHandler.call(id, [width, height]);
					});
					FlxG.signals.gameResized.add(cast sbCallbacks.get(id));
				case "onGameReset":
					sbCallbacks.set(id, function() {
						ModuleHandler.call(id);
					});
					FlxG.signals.preGameReset.add(cast sbCallbacks.get(id));
				case "onGameResetPost":
					sbCallbacks.set(id, function() {
						ModuleHandler.call(id);
					});
					FlxG.signals.postGameReset.add(cast sbCallbacks.get(id));
				case "onGameStart":
					sbCallbacks.set(id, function() {
						ModuleHandler.call(id);
					});
					FlxG.signals.preGameStart.add(cast sbCallbacks.get(id));
				case "onGameStartPost":
					sbCallbacks.set(id, function() {
						ModuleHandler.call(id);
					});
					FlxG.signals.postGameStart.add(cast sbCallbacks.get(id));
				case "onUpdate":
					sbCallbacks.set(id, function() {
						ModuleHandler.call(id, [FlxG.elapsed]);
					});
					FlxG.signals.preUpdate.add(cast sbCallbacks.get(id));
				case "onUpdatePost":
					sbCallbacks.set(id, function() {
						ModuleHandler.call(id, [FlxG.elapsed]);
					});
					FlxG.signals.postUpdate.add(cast sbCallbacks.get(id));
				case "onDraw":
					sbCallbacks.set(id, function() {
						ModuleHandler.call(id);
					});
					FlxG.signals.preDraw.add(cast sbCallbacks.get(id));
				case "onDrawPost":
					sbCallbacks.set(id, function() {
						ModuleHandler.call(id);
					});
					FlxG.signals.postDraw.add(cast sbCallbacks.get(id));
				case "onFocusGained":
					sbCallbacks.set(id, function() {
						ModuleHandler.call(id);
					});
					FlxG.signals.focusGained.add(cast sbCallbacks.get(id));
				case "onFocusLost":
					sbCallbacks.set(id, function() {
						ModuleHandler.call(id);
					});
					FlxG.signals.focusLost.add(cast sbCallbacks.get(id));
				case _:
			}
		}
	}

	public static function call(name:String, ?args:Array<Dynamic>):Dynamic {
		for(module in moduleArray) {
			if(module.active) {
				var func:Dynamic = Reflect.getProperty(module, name);
				if(Reflect.isFunction(func)) {
					ModuleAgency.runThrow(function() {
						Reflect.callMethod(null, func, args ?? []);
					}, "hscript");
				}
			}
		}

		return null;
	}
}
