package codename.funkin.editors;

import flixel.FlxState;

#if mobile
import codename.funkin.options.PlayerSettings;

/** Warns before opening editor screens that still require precise pointer or text input. */
final class MobileEditorWarning
{
	public static function open(parent:FlxState, onContinue:Void->Void):Void
	{
		if (!PlayerSettings.solo.controls.touchC) {
			onContinue();
			return;
		}

		inline function translated(id:String, fallback:String):String
			return TU.exists(id) ? TU.translate(id) : fallback;

		parent.openSubState(new UIWarningSubstate(
			translated("editor.warnings.mkRequirement-title", "Mouse or keyboard may be required"),
			translated("editor.warnings.mkRequirement-body",
				"This editor supports touch navigation, but some precise editing and text-entry actions still require a connected mouse or keyboard. Continue?"),
			[
				{
					label: translated("editor.ok", "Continue"),
					color: 0xFFFFFF00,
					onClick: _ -> onContinue()
				},
				{
					label: translated("editor.cancel", "Cancel"),
					color: 0xFFFFFF00,
					onClick: _ -> {}
				}
			], false, 1));
	}
}
#else
final class MobileEditorWarning
{
	public static inline function open(parent:FlxState, onContinue:Void->Void):Void
		onContinue();
}
#end
