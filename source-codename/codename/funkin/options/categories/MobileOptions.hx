package codename.funkin.options.categories;

#if TOUCH_CONTROLS
import codename.funkin.options.type.OptionType;
import codename.mobile.MobileSettingsRuntime;

private enum MobileSettingEditor
{
	Choices(option:String, values:Array<Dynamic>, captions:Array<String>, changed:Null<Dynamic->Void>);
	Number(option:String, minimum:Float, maximum:Float, increment:Float, changed:Null<Float->Void>);
	Toggle(option:String, changed:Null<Void->Void>);
}

private typedef MobileSettingRow =
{
	final title:String;
	final explanation:String;
	final editor:MobileSettingEditor;
}

private typedef MobileSettingSection =
{
	final rows:Array<MobileSettingRow>;
}

/** NovaFlare's declarative settings page for Codename touch controls. */
class MobileOptions extends TreeMenuScreen
{
	public function new(title:String = 'Mobile Options', explanation:String = 'Configure Codename touch controls.')
	{
		super(title, explanation);
		var sections = describeSections();
		for (sectionIndex in 0...sections.length)
		{
			if (sectionIndex > 0) add(new Separator(36));
			for (row in sections[sectionIndex].rows) add(createEditor(row));
		}
	}

	static function describeSections():Array<MobileSettingSection>
	{
		var sections:Array<MobileSettingSection> = [
			{rows: [
				{
					title: 'Pad Opacity',
					explanation: 'Adjusts the visibility of menu and editor touch buttons.',
					editor: Number('touchPadAlpha', 0, 1, 0.1, MobileSettingsRuntime.applyPadOpacity)
				},
				{
					title: 'Classic Skin',
					explanation: 'Uses the classic button artwork instead of NovaFlare\'s current touch style.',
					editor: Toggle('oldPadTexture', MobileSettingsRuntime.reloadPadGraphics)
				}
			]},
			{rows: [
				{
					title: 'Hitbox Style',
					explanation: 'Selects how the four gameplay touch lanes react when pressed.',
					editor: Choices('hitboxType',
						['noGradient', 'noGradientOld', 'gradient', 'hidden'],
						['Soft', 'Solid', 'Gradient', 'Hidden'], null)
				},
				{
					title: 'Hitbox Opacity',
					explanation: 'Adjusts the strength of the gameplay touch feedback.',
					editor: Number('hitboxAlpha', 0, 1, 0.1, null)
				},
				{
					title: 'Extra Zones',
					explanation: 'Adds one or two auxiliary touch zones outside the four note lanes.',
					editor: Choices('extraHints', ['NONE', 'SINGLE', 'DOUBLE'], ['Off', 'One', 'Two'], null)
				},
				{
					title: 'Zones Below',
					explanation: 'Places extra zones below the note lanes when enabled, or above them when disabled.',
					editor: Toggle('hitboxPos', null)
				}
			]}
		];

		#if mobile
		sections.push({rows: [{
			title: 'Screen Sleep',
			explanation: 'Lets Android turn the display off after the system idle timeout.',
			editor: Toggle('screenTimeOut', MobileSettingsRuntime.applyScreenTimeout)
		}]});
		#end
		return sections;
	}

	static function createEditor(row:MobileSettingRow):OptionType
	{
		return switch (row.editor)
		{
			case Choices(option, values, captions, changed):
				new ArrayOption(row.title, row.explanation, values, captions, option, changed);
			case Number(option, minimum, maximum, increment, changed):
				new NumOption(row.title, row.explanation, minimum, maximum, increment, option, changed);
			case Toggle(option, changed):
				new Checkbox(row.title, row.explanation, option, changed);
		}
	}
}
#end
