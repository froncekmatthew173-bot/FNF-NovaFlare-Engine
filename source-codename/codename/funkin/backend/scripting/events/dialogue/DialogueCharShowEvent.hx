package codename.funkin.backend.scripting.events.dialogue;

import codename.funkin.game.cutscenes.dialogue.DialogueCharacter.DialogueCharAnimContext;

final class DialogueCharShowEvent extends CancellableEvent
{
	public var x:Float;

	public var y:Float;

	public var animation:String;

	public var lastAnimContext:DialogueCharAnimContext;
}