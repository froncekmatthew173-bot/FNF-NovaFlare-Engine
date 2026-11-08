package gameanalytics;

import lime.app.Application;

/**
 * Owns analytics for the whole application rather than one engine's state
 * hierarchy. NovaFlare, Origin Funkin and Codename all pass through Main, but
 * they do not share a MusicBeatState implementation.
 */
class GAAppLifecycle
{
	static var installed:Bool = false;
	static var elapsedMs:Float = 0;
	static var startEventSent:Bool = false;

	public static function install():Void
	{
		if (installed)
			return;

		installed = true;
		GABridge.init();

		final application = Application.current;
		if (application == null)
			return;

		application.onUpdate.add(update);
		application.onExit.add(onExit);

		if (application.window != null)
		{
			application.window.onDeactivate.add(onDeactivate);
			application.window.onActivate.add(onActivate);
		}
	}

	static function update(deltaTimeMs:Float):Void
	{
		if (!startEventSent)
		{
			startEventSent = true;
			GABridge.sendDesign('engine:start:${currentChain()}');
			// Do not make a short launch wait for the periodic batch interval.
			GABridge.flush();
		}

		if (deltaTimeMs <= 0)
			return;

		elapsedMs += deltaTimeMs;
		if (elapsedMs >= 1000 / 60)
		{
			GABridge.update(elapsedMs / 1000);
			elapsedMs = 0;
		}
	}

	static function onDeactivate():Void
	{
		GABridge.onPause();
		GABridge.flush();
		elapsedMs = 0;
	}

	static function onActivate():Void
	{
		elapsedMs = 0;
	}

	static function onExit(_exitCode:Int):Void
	{
		GABridge.flush();
	}

	static function currentChain():String
	{
		#if CODENAME_ENGINE_COMPAT
		if (codenamechain.CodeNameMode.active)
			return 'codename';
		#end
		if (originfunkin.OriginFunkinMode.active)
			return 'origin';
		return 'novaflare';
	}
}
