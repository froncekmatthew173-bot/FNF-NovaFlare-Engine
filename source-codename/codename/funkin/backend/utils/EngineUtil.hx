package codename.funkin.backend.utils;

import codename.funkin.backend.scripting.MultiThreadedScript;
import codename.funkin.backend.scripting.Script;

final class EngineUtil {
	/**
	 * Starts a new multithreaded script.
	 * This script will share all the variables with the current one, which means already existing callbacks will be replaced by new ones on conflict.
	 * @param path
	 */
	public static function startMultithreadedScript(path:String) {
		return new MultiThreadedScript(path, Script.curScript);
	}
}