package mobile.funkin.backend.utils;

#if TOUCH_CONTROLS
import flixel.util.FlxSave;
import haxe.ds.Map;
import mobile.funkin.backend.system.input.MobileInputID;
import mobile.funkin.backend.utils.MobileLayout.MobileAnchor;
import mobile.funkin.backend.utils.MobileLayout.MobileButtonSlot;

using StringTools;

/**
 * Registry for NovaFlare's Codename touch layouts.
 *
 * Layouts are compiled descriptors rather than asset JSON. This keeps startup
 * deterministic on Android and gives the runtime typed input identifiers.
 */
final class MobileData
{
	public static var actionModes:Map<String, MobileLayout> = new Map();
	public static var dpadModes:Map<String, MobileLayout> = new Map();
	public static var save:FlxSave;

	public static function init():Void
	{
		clearTouchPadData();

		if (save == null)
		{
			save = new FlxSave();
			save.bind('CodenameMobileControls', #if sys 'NovaFlareEngine/Codename' #else 'NovaFlareEngine' #end);
		}

		registerDPad('LEFT_FULL', [
			bl(MobileInputID.UP, 'up', 98, 315, 0xFF12FA05),
			bl(MobileInputID.LEFT, 'left', 0, 220, 0xFFC24B99),
			bl(MobileInputID.RIGHT, 'right', 196, 220, 0xFFF9393F),
			bl(MobileInputID.DOWN, 'down', 98, 124, 0xFF00FFFF)
		]);
		registerDPad('LEFT_RIGHT', [
			bl(MobileInputID.LEFT, 'left', 0, 133, 0xFFC24B99),
			bl(MobileInputID.RIGHT, 'right', 127, 133, 0xFFF9393F)
		]);
		registerDPad('MENU_FULL', [
			bl(MobileInputID.UP, 'up', 0, 248, 0xFF12FA05),
			bl(MobileInputID.DOWN, 'down', 0, 124, 0xFF00FFFF),
			bl(MobileInputID.LEFT, 'left', 124, 124, 0xFFC24B99),
			bl(MobileInputID.RIGHT, 'right', 248, 124, 0xFFF9393F)
		]);
		registerDPad('RIGHT_FULL', [
			br(MobileInputID.UP, 'up', 258, 406, 0xFF12FA05),
			br(MobileInputID.LEFT, 'left', 384, 307, 0xFFC24B99),
			br(MobileInputID.RIGHT, 'right', 132, 307, 0xFFF9393F),
			br(MobileInputID.DOWN, 'down', 258, 199, 0xFF00FFFF)
		]);
		registerDPad('UP_DOWN', [
			bl(MobileInputID.UP, 'up', 0, 248, 0xFF12FA05),
			bl(MobileInputID.DOWN, 'down', 0, 124, 0xFF00FFFF)
		]);

		registerActions('A', [br(MobileInputID.A, 'a', 124, 124, 0xFFFF0000)]);
		registerActions('A_B', [
			br(MobileInputID.A, 'a', 124, 124, 0xFFFF0000),
			br(MobileInputID.B, 'b', 248, 124, 0xFFFFCB00)
		]);
		registerActions('A_B_C', [
			br(MobileInputID.A, 'a', 124, 124, 0xFFFF0000),
			br(MobileInputID.B, 'b', 248, 124, 0xFFFFCB00),
			br(MobileInputID.C, 'c', 372, 124, 0xFF44FF00)
		]);
		registerActions('A_B_C_D_V_X_Y_Z', [
			br(MobileInputID.V, 'v', 496, 248, 0xFF49A9B2),
			br(MobileInputID.D, 'd', 496, 124, 0xFF0078FF),
			br(MobileInputID.X, 'x', 372, 248, 0xFF99062D),
			br(MobileInputID.C, 'c', 372, 124, 0xFF44FF00),
			br(MobileInputID.Y, 'y', 248, 248, 0xFF4A35B9),
			br(MobileInputID.B, 'b', 248, 124, 0xFFFFCB00),
			br(MobileInputID.Z, 'z', 124, 248, 0xFFCCB98E),
			br(MobileInputID.A, 'a', 124, 124, 0xFFFF0000)
		]);
		registerActions('A_B_C_X_Y_Z', [
			br(MobileInputID.X, 'x', 372, 248, 0xFF99062D),
			br(MobileInputID.C, 'c', 372, 124, 0xFF44FF00),
			br(MobileInputID.Y, 'y', 248, 248, 0xFF4A35B9),
			br(MobileInputID.B, 'b', 248, 124, 0xFFFFCB00),
			br(MobileInputID.Z, 'z', 124, 248, 0xFFCCB98E),
			br(MobileInputID.A, 'a', 124, 124, 0xFFFF0000)
		]);
		registerActions('A_B_M_E', [
			br(MobileInputID.A, 'a', 124, 124, 0xFFFF0000),
			br(MobileInputID.B, 'b', 248, 124, 0xFFFFCB00),
			br(MobileInputID.M, 'm', 372, 124, 0xFF00BBFF),
			br(MobileInputID.E, 'e', 496, 124, 0xFFFF7D00)
		]);
		registerActions('A_B_X_Y', [
			br(MobileInputID.A, 'a', 124, 124, 0xFFFF0000),
			br(MobileInputID.B, 'b', 248, 124, 0xFFFFCB00),
			br(MobileInputID.X, 'x', 372, 124, 0xFF99062D),
			br(MobileInputID.Y, 'y', 496, 124, 0xFF4A35B9)
		]);
		registerActions('B', [br(MobileInputID.B, 'b', 124, 124, 0xFFFFCB00)]);
		registerActions('B_C', [
			br(MobileInputID.C, 'c', 248, 124, 0xFF44FF00),
			br(MobileInputID.B, 'b', 124, 124, 0xFFFFCB00)
		]);
		registerActions('P', [tr(MobileInputID.P, 'p', 124, 2, 0xFFE5DE00)]);
		registerActions('P_C', [
			tr(MobileInputID.C, 'c', 248, 2, 0xFF44FF00),
			tr(MobileInputID.P, 'p', 124, 2, 0xFFE5DE00)
		]);
	}

	public static function clearTouchPadData():Void
	{
		dpadModes.clear();
		actionModes.clear();
	}

	public static function registerDPad(id:String, slots:Array<MobileButtonSlot>):Void
		dpadModes.set(normalize(id), new MobileLayout(normalize(id), slots));

	public static function registerActions(id:String, slots:Array<MobileButtonSlot>):Void
		actionModes.set(normalize(id), new MobileLayout(normalize(id), slots));

	public static inline function normalize(id:String):String
		return id == null ? 'NONE' : id.trim().toUpperCase();

	static inline function bl(input:MobileInputID, symbol:String, x:Float, fromBottom:Float, tint:Int):MobileButtonSlot
		return new MobileButtonSlot(input, symbol, BOTTOM_LEFT, x, fromBottom, tint);

	static inline function br(input:MobileInputID, symbol:String, fromRight:Float, fromBottom:Float, tint:Int):MobileButtonSlot
		return new MobileButtonSlot(input, symbol, BOTTOM_RIGHT, fromRight, fromBottom, tint);

	static inline function tr(input:MobileInputID, symbol:String, fromRight:Float, y:Float, tint:Int):MobileButtonSlot
		return new MobileButtonSlot(input, symbol, TOP_RIGHT, fromRight, y, tint);
}

typedef TouchButtonsData = MobileLayout;
typedef ButtonsData = MobileButtonSlot;
#end
