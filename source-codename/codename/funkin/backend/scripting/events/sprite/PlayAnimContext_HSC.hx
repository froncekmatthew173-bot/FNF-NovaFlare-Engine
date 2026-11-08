package codename.funkin.backend.scripting.events.sprite;

#if CODENAME_ENGINE_COMPAT
/**
 * Runtime reflection surface for PlayAnimContext.
 *
 * The generic hscript-improved shadow cannot emit native static fields named
 * `NONE` or `SING` on Windows because platform/UCRT headers define both names
 * as macros. Getters preserve the HScript-facing API without emitting either
 * conflicting C++ member token.
 */
class PlayAnimContext_HSC
{
	public static var NONE(get, never):PlayAnimContext;
	public static var SING(get, never):PlayAnimContext;
	public static var DANCE(get, never):PlayAnimContext;
	public static var MISS(get, never):PlayAnimContext;
	public static var LOCK(get, never):PlayAnimContext;

	private static inline function get_NONE():PlayAnimContext
		return PlayAnimContext.NONE;
	private static inline function get_SING():PlayAnimContext
		return PlayAnimContext.SING;
	private static inline function get_DANCE():PlayAnimContext
		return PlayAnimContext.DANCE;
	private static inline function get_MISS():PlayAnimContext
		return PlayAnimContext.MISS;
	private static inline function get_LOCK():PlayAnimContext
		return PlayAnimContext.LOCK;
}
#end
