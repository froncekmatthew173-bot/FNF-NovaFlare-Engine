package codename.funkin.backend.scripting.events.stage;

import codename.funkin.game.Stage;
import haxe.xml.Access;

final class StageXMLEvent extends CancellableEvent {
	/**
	 * The stage instance
	 */
	public var stage:Stage;

	/**
	 * The xml
	 */
	public var xml:Access;

	/**
	 * The object which was parsed
	 */
	public var elems:Array<Access>;
}