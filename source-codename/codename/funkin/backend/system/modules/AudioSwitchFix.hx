package codename.funkin.backend.system.modules;

import flixel.FlxState;
import flixel.sound.FlxSound;
import codename.funkin.backend.utils.NativeAPI;
import lime.media.AudioManager;
import lime.media.AudioSource;
import haxe.Timer;

/**
 * if you are stealing this keep this comment at least please lol
 *
 * hi gray itsa me yoshicrafter29 i fixed it hehe
 */
@:dox(hide)
class AudioSwitchFix {
	public static function onAudioDisconnected() @:privateAccess {
		// lime 8.x removed AudioSource.activeSources - audio state save/restore disabled
		AudioManager.shutdown();
		AudioManager.init();
		
		Main.changeID++;
		Main.audioDisconnected = false;
	}

	private static var timer:Timer;

	private static function onRun() if (Main.audioDisconnected) onAudioDisconnected();
	public static function init() {
		NativeAPI.registerAudio();
		if (timer == null) (timer = new Timer(1000)).run = onRun;
	}
}