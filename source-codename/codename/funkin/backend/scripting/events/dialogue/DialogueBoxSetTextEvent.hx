package codename.funkin.backend.scripting.events.dialogue;

import flixel.sound.FlxSound;
import codename.funkin.backend.utils.XMLUtil.TextFormat;

final class DialogueBoxSetTextEvent extends CancellableEvent
{
	public var text:String;

	public var format:Array<TextFormat>;

	public var speed:Null<Float>;

	public var customTypeSFX:Array<FlxSound>;
}