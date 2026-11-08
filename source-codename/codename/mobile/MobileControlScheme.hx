package codename.mobile;

#if TOUCH_CONTROLS
/**
 * Declarative mobile controls for a state or substate.
 *
 * The names intentionally remain strings because softcoded states already use
 * `setTouchPadMode(dpad, actions)`.  The resolver is the single place where a
 * built-in screen chooses its default controls; rendering and input ownership
 * are handled elsewhere.
 */
class MobileControlScheme
{
	public final dpad:String;
	public final actions:String;
	public final separateCamera:Bool;
	public final gameplayHitbox:Bool;

	public function new(dpad:String, actions:String, separateCamera:Bool = false, gameplayHitbox:Bool = false)
	{
		this.dpad = dpad;
		this.actions = actions;
		this.separateCamera = separateCamera;
		this.gameplayHitbox = gameplayHitbox;
	}

	public static function forState(owner:Dynamic):Null<MobileControlScheme>
	{
		var name = className(owner);
		return switch (name)
		{
			// Gameplay deliberately exposes pause only. RESET/C remains available
			// to non-gameplay screens that explicitly request it.
			case "codename.funkin.game.PlayState": new MobileControlScheme("NONE", "P", true, true);
			case "codename.funkin.menus.MainMenuState": new MobileControlScheme("UP_DOWN", "A_B_M_E");
			case "codename.funkin.menus.FreeplayState": new MobileControlScheme("LEFT_FULL", "A_B_C_X_Y_Z");
			case "codename.funkin.menus.StoryMenuState": new MobileControlScheme("LEFT_FULL", "A_B");
			case "codename.funkin.menus.GitarooPause": new MobileControlScheme("LEFT_RIGHT", "A");
			case "codename.funkin.menus.credits.CreditsMain": new MobileControlScheme("UP_DOWN", "A_B_C", true);
			case "codename.funkin.options.OptionsMenu": new MobileControlScheme("MENU_FULL", "A_B", true);
			case "codename.funkin.backend.system.updating.UpdateAvailableScreen": new MobileControlScheme("LEFT_FULL", "A_B", true);
			case "codename.funkin.editors.character.CharacterSelection"
				| "codename.funkin.editors.alphabet.AlphabetSelection"
				| "codename.funkin.editors.charter.CharterSelection"
				| "codename.funkin.editors.stage.StageSelection":
				new MobileControlScheme("UP_DOWN", "A_B", true);
			default:
				if (name != "codename.funkin.editors.charter.Charter" && extendsClass(owner, "codename.funkin.editors.ui.UIState"))
					new MobileControlScheme("NONE", "B", true);
				else
					null;
		}
	}

	public static function forSubstate(owner:Dynamic):Null<MobileControlScheme>
	{
		return switch (className(owner))
		{
			case "codename.funkin.menus.ModSwitchMenu" | "codename.funkin.editors.EditorPicker":
				new MobileControlScheme("UP_DOWN", "A_B", true);
			case "codename.funkin.menus.PlaytestingWarningSubstate":
				new MobileControlScheme("LEFT_RIGHT", "A", true);
			case "codename.funkin.menus.PauseSubState":
				new MobileControlScheme("UP_DOWN", "A_B", true);
			case "codename.funkin.game.GameOverSubstate":
				new MobileControlScheme("NONE", "A_B", true);
			case "codename.funkin.options.keybinds.KeybindsOptions":
				new MobileControlScheme("LEFT_FULL", "A_B", true);
			case "codename.funkin.options.keybinds.ChangeKeybindSubState":
				new MobileControlScheme("NONE", "B", true);
			case "codename.funkin.editors.ui.MobileFilePickerSubstate":
				new MobileControlScheme("UP_DOWN", "A_B", true);
			case "codename.funkin.editors.ui.UIWarningSubstate"
				| "codename.funkin.editors.ui.UIContextMenu"
				| "codename.funkin.editors.charter.CharterEventScreenNew":
				new MobileControlScheme("NONE", "B", true);
			default:
				if (extendsClass(owner, "codename.funkin.editors.ui.UISubstateWindow"))
					new MobileControlScheme("NONE", "B", true);
				else if (extendsClass(owner, "codename.funkin.game.cutscenes.Cutscene"))
					new MobileControlScheme("NONE", "A_B", true);
				else
					null;
		}
	}

	static inline function className(owner:Dynamic):String
		return Type.getClassName(Type.getClass(owner));

	public static function extendsClass(owner:Dynamic, expected:String):Bool
	{
		var type:Class<Dynamic> = Type.getClass(owner);
		while (type != null)
		{
			if (Type.getClassName(type) == expected) return true;
			type = Type.getSuperClass(type);
		}
		return false;
	}
}
#end
