package gameanalytics;

#if macro
import haxe.macro.Compiler;
import haxe.macro.Context;
import haxe.macro.Expr;
#end

/** Reads analytics credentials from the build environment, never from private sources. */
class GACompileConfig
{
	#if macro
	public static function configure():Void
	{
		final keys = credentials();
		if (keys.gameKey == '' || keys.secretKey == '') return;

		Compiler.define('GAMEANALYTICS_ENABLED');
		if (Context.defined('debug'))
			Compiler.define('GAMEANALYTICS_VERBOSE');
	}

	/** Injects credentials into typed fields without generating a credential source file. */
	public static macro function buildConfig():Array<Field>
	{
		final keys = credentials();
		final fields = Context.getBuildFields();
		for (field in fields)
		{
			final value:Null<String> = switch (field.name)
			{
				case 'GAME_KEY': keys.gameKey;
				case 'SECRET_KEY': keys.secretKey;
				case 'BUILD_VERSION': environment('NOVA_GA_BUILD_VERSION');
				default: null;
			};
			if (value == null || (field.name == 'BUILD_VERSION' && value == '')) continue;
			switch (field.kind)
			{
				case FVar(type, _): field.kind = FVar(type, macro $v{value});
				default:
			}
		}
		return fields;
	}

	static function environment(name:String):String
	{
		final value = Sys.getEnv(name);
		return value == null ? '' : StringTools.trim(value);
	}

	static function credentials():{gameKey:String, secretKey:String}
	{
		final gameKey = environment('NOVA_GA_GAME_KEY');
		final secretKey = environment('NOVA_GA_SECRET_KEY');
		if ((gameKey == '') != (secretKey == '') ||
			((gameKey == '' || secretKey == '') && environment('NOVA_GA_REQUIRED') == '1'))
			Context.error('Set both NOVA_GA_GAME_KEY and NOVA_GA_SECRET_KEY in the build environment.', Context.currentPos());
		return {gameKey: gameKey, secretKey: secretKey};
	}

	/** Compile-time assertion used by build verification. */
	public static function verify(expected:Bool):Void
	{
		final enabled = Context.defined('GAMEANALYTICS_ENABLED');
		if (enabled != expected)
			Context.error('Expected GameAnalytics enabled=$expected, got $enabled', Context.currentPos());
	}
	#end
}
