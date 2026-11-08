package codename.funkin.backend.scripting.events.dialogue;

import codename.funkin.game.cutscenes.dialogue.DialogueCharacter.DialogueCharAnimContext;

final class DialogueCharHideEvent extends CancellableEvent
{
	public var animation:String;

	public var lastAnimContext:DialogueCharAnimContext;
}