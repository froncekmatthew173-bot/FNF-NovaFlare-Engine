package lime.system;

import lime.app.Event;

/**
 * Shim for lime.system.WorkOutput which was removed in Lime 8.x.
 * Provides sendComplete/sendError callbacks used by LoadingState.
 */
class WorkOutput {
	public var onComplete:Event<Void->Void> = cast new Event<Void->Void>();
	public var onError:Event<Void->Void> = cast new Event<Void->Void>();
	
	public function sendComplete(data:Dynamic):Void {
		onComplete.dispatch();
	}
	
	public function sendError(data:Dynamic):Void {
		onError.dispatch();
	}
}
