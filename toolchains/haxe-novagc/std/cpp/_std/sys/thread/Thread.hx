/*
 * Copyright (C)2005-2019 Haxe Foundation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

package sys.thread;

#if hxcpp_zgc

private typedef ThreadImpl = ZgcThread;

abstract Thread(ThreadImpl) from ThreadImpl {
    public var events(get, never):EventLoop;

    inline function get_events():EventLoop {
        if (this.events == null)
            throw new NoEventLoopException();
        return this.events;
    }

    public inline function sendMessage(message:Dynamic):Void {
        this.sendMessage(message);
    }

    public static inline function current():Thread {
        return ZgcThread.current();
    }

    public static inline function create(job:()->Void):Thread {
        return ZgcThread.create(job, false);
    }

    public static inline function runWithEventLoop(job:()->Void):Void {
        ZgcThread.runWithEventLoop(job);
    }

    public static inline function createWithEventLoop(job:()->Void):Thread {
        return ZgcThread.create(job, true);
    }

    public static inline function readMessage(block:Bool):Dynamic {
        return ZgcThread.readMessage(block);
    }

    @:keep
    public static function processEvents():Void {
        ZgcThread.current().events.loop();
    }
}

private class ZgcThread {
    public var handle:Dynamic;
    public var events(default, null):Null<EventLoop>;
    static var mainThread:ZgcThread;

    static function __init__():Void {
        mainThread = new ZgcThread(
            untyped __global__.__hxcpp_thread_current(), new EventLoop());
    }

    function new(handle:Dynamic, events:Null<EventLoop>) {
        this.handle = handle;
        this.events = events;
    }

    public static function current():ZgcThread {
        final handle:Dynamic = untyped __global__.__hxcpp_thread_current();
        if (handle == mainThread.handle)
            return mainThread;
        return new ZgcThread(handle, null);
    }

    public static function create(job:()->Void, withEventLoop:Bool):ZgcThread {
        final thread = new ZgcThread(
            null, withEventLoop ? new EventLoop() : null);
        thread.handle = untyped __global__.__hxcpp_thread_create(() -> {
            job();
            if (thread.events != null)
                thread.events.loop();
        });
        return thread;
    }

    public static function runWithEventLoop(job:()->Void):Void {
        final thread = current();
        if (thread.events == null) {
            thread.events = new EventLoop();
            try {
                job();
                thread.events.loop();
            } catch (error) {
                thread.events = null;
                throw error;
            }
            thread.events = null;
        } else {
            job();
        }
    }

    public inline function sendMessage(message:Dynamic):Void {
        untyped __global__.__hxcpp_thread_send(handle, message);
    }

    public static inline function readMessage(block:Bool):Dynamic {
        return untyped __global__.__hxcpp_thread_read_message(block);
    }
}

#else

private typedef ThreadImpl = HaxeThread;

abstract Thread(ThreadImpl) from ThreadImpl {
	public var events(get,never):EventLoop;

	public inline function sendMessage(msg:Dynamic):Void {
		this.sendMessage(msg);
	}

	public static inline function current():Thread {
		return HaxeThread.current();
	}

	public static inline function create(job:()->Void):Thread {
		return HaxeThread.create(job, false);
	}

	public static inline function runWithEventLoop(job:()->Void):Void {
		HaxeThread.runWithEventLoop(job);
	}

	public static inline function createWithEventLoop(job:()->Void):Thread {
		return HaxeThread.create(job, true);
	}

	public static function readMessage(block:Bool):Dynamic {
		return HaxeThread.readMessage(block);
	}

	function get_events():EventLoop {
		if(this.events == null)
			throw new NoEventLoopException();
		return this.events;
	}

	@:keep
	static public function processEvents() {
		HaxeThread.current().events.loop();
	}
}

@:callable
@:coreType
private abstract NativeThreadHandle {}

private typedef ThreadHandle = NativeThreadHandle;


private class HaxeThread {
	static var threads:Array<{thread:HaxeThread, handle:ThreadHandle}>;
	static var threadsMutex:Mutex;
	static var mainThreadHandle:ThreadHandle;
	static var mainThread:HaxeThread;

	static function __init__() {
		threads = [];
		threadsMutex = new Mutex();
		mainThreadHandle = currentHandle();
		mainThread = new HaxeThread(currentHandle());		
		mainThread.events = new EventLoop();
	}

	public var events(default,null):Null<EventLoop>;
	public var handle:ThreadHandle;
	final messages = new Deque<Dynamic>();

	static public function current():HaxeThread {
		var handle = currentHandle();
		if(handle == mainThreadHandle) {
			return mainThread;
		}
		threadsMutex.acquire();
		var thread = null;
		for(item in threads) {
			if(item.handle == handle) {
				thread = item.thread;
				break;
			}
		}
		if(thread == null) {
			thread = new HaxeThread(handle);
			threads.push({thread:thread, handle:handle});
		}
		threadsMutex.release();
		return thread;
	}

	public static function create(job:()->Void, withEventLoop:Bool):Thread {
		var item = {handle:null, thread:new HaxeThread(null)};
		threadsMutex.acquire();
		var index = threads.push(item);
		threadsMutex.release();
		if(withEventLoop)
			item.thread.events = new EventLoop();
		item.handle = createHandle(() -> {
			if(item.thread.handle == null) {
				item.handle = currentHandle();
				item.thread.handle = item.handle;
			}
			try {
				job();
				if(withEventLoop)
					item.thread.events.loop();
			} catch(e) {
				dropThread(item, index);
				throw e;
			}
			dropThread(item, index);
		});
		item.thread.handle = item.handle;
		return item.thread;
	}

	public static function runWithEventLoop(job:()->Void):Void {
		var thread = current();
		if(thread.events == null) {
			thread.events = new EventLoop();
			try {
				job();
				thread.events.loop();
				thread.events = null;
			} catch(e) {
				thread.events = null;
				throw e;
			}
		} else {
			job();
		}
	}

	static function dropThread(item, probableIndex:Int) {
		threadsMutex.acquire();
		if(threads[probableIndex] == item) {
			threads.splice(probableIndex, 1);
		} else {
			for(i => item2 in threads) {
				if(item2 == item) {
					threads.splice(i, 1);
					break;
				}
			}
		}
		threadsMutex.release();
	}

	function new(h:ThreadHandle):Void {
		handle = h;
	}

	public inline function sendMessage(msg:Dynamic):Void {
		messages.add(msg);
	}

	static #if !scriptable inline #end function currentHandle():ThreadHandle {
		return untyped __global__.__hxcpp_thread_current();
	}

	static #if !scriptable inline #end function createHandle(callb:Void->Void):ThreadHandle {
		return untyped __global__.__hxcpp_thread_create(callb);
	}

	public static #if !scriptable inline #end function readMessage(block:Bool):Dynamic {
		return current().messages.pop(block);
	}
}

#end
