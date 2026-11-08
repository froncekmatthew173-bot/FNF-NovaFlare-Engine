package modchart.backend.standalone;

import haxe.macro.Compiler;

/** Select the modchart backend for the state that owns the new Manager. */
class Adapter {
	public static var instance:IAdapter;
	private static var ENGINE_NAME:String = Compiler.getDefine("FM_ENGINE");

	public static function init() {
		#if CODENAME_ENGINE_COMPAT
		if (Std.isOfType(flixel.FlxG.state, codename.funkin.game.PlayState)) {
			if (!Std.isOfType(instance, codenamechain.CodeNameModchartAdapter))
				instance = new codenamechain.CodeNameModchartAdapter();
			return;
		}
		// A Manager in NovaFlare must not reuse the previous CodeName backend.
		if (Std.isOfType(instance, codenamechain.CodeNameModchartAdapter))
			instance = null;
		#end
		if (instance != null) return;

		final className = ENGINE_NAME.substr(0, 1).toUpperCase() + ENGINE_NAME.substr(1).toLowerCase();
		final adapterClass = Type.resolveClass('modchart.backend.standalone.adapters.${ENGINE_NAME.toLowerCase()}.$className');
		if (adapterClass == null) throw 'Adapter not found for $ENGINE_NAME';
		instance = Type.createInstance(adapterClass, []);
	}
}
