package codename.funkin.backend.utils;

#if CODENAME_ENGINE_COMPAT
import codename.funkin.backend.utils.CoolUtil.CoolSfx;

/** Runtime reflection surface for the CoolSfx abstract used by older mods. */
class CoolSfx_HSC
{
	public static final SCROLL:CoolSfx = @:privateAccess CoolSfx.SCROLL;
	public static final CONFIRM:CoolSfx = @:privateAccess CoolSfx.CONFIRM;
	public static final CANCEL:CoolSfx = @:privateAccess CoolSfx.CANCEL;
	public static final CHECKED:CoolSfx = @:privateAccess CoolSfx.CHECKED;
	public static final UNCHECKED:CoolSfx = @:privateAccess CoolSfx.UNCHECKED;
	public static final WARNING:CoolSfx = @:privateAccess CoolSfx.WARNING;
}
#end
