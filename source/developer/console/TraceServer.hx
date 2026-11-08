package developer.console;

#if sys
import sys.net.Host;
import sys.net.Socket;
import sys.thread.Mutex;
import sys.thread.Thread;
#end

class TraceServer {
	#if sys
	private static var server:Socket = null;
	private static var clients:Array<Socket> = [];
	private static var clientsLock:Mutex = new Mutex();
	private static var isRunning:Bool = false;

	public static function start(port:Int = 1145):Void {
		if (isRunning) return;

		try {
			server = new Socket();
			server.bind(new Host("0.0.0.0"), port);
			server.listen(8);
			isRunning = true;

			Console.logLevel("NET", 'Trace server listening on port $port', 0x8BD3FF);
			Thread.create(acceptConnections);
		} catch (e:Dynamic) {
			isRunning = false;
			server = null;
			Console.logLevel("ERROR", 'Trace server failed to start: $e', 0xFF6B6B);
		}
	}

	public static function stop():Void {
		isRunning = false;

		if (server != null) {
			try {
				server.close();
			} catch (_:Dynamic) {}
			server = null;
		}

		clientsLock.acquire();
		var snapshot = clients.copy();
		clients = [];
		clientsLock.release();

		for (client in snapshot) {
			closeClient(client);
		}
	}

	public static function sendTraceMessage(level:String, message:String, color:Int):Void {
		if (!isRunning) return;

		clientsLock.acquire();
		var snapshot = clients.copy();
		clientsLock.release();

		if (snapshot.length == 0) return;

		var deadClients:Array<Socket> = [];
		for (client in snapshot) {
			try {
				sendFormattedMessage(client, level, message, color);
			} catch (_:Dynamic) {
				deadClients.push(client);
			}
		}

		for (client in deadClients) {
			removeClient(client);
			closeClient(client);
		}
	}

	private static function acceptConnections():Void {
		while (isRunning) {
			try {
				var client = server.accept();
				addClient(client);
				sendFormattedMessage(client, "INFO", "Connected to NovaFlare trace server.", 0x8BD3FF);
				Console.logLevel("NET", "Trace client connected", 0x8BD3FF);
			} catch (e:Dynamic) {
				if (isRunning) {
					Console.logLevel("WARN", 'Trace server accept failed: $e', 0xFFD166);
				}
			}
		}
	}

	private static function addClient(client:Socket):Void {
		clientsLock.acquire();
		clients.push(client);
		clientsLock.release();
	}

	private static function removeClient(client:Socket):Void {
		clientsLock.acquire();
		clients.remove(client);
		clientsLock.release();
	}

	private static function closeClient(client:Socket):Void {
		try {
			client.close();
		} catch (_:Dynamic) {}
	}

	private static function sendFormattedMessage(client:Socket, level:String, message:String, color:Int):Void {
		var safeMessage = StringTools.replace(message, "\r\n", "\\n");
		safeMessage = StringTools.replace(safeMessage, "\r", "\\n");
		safeMessage = StringTools.replace(safeMessage, "\n", "\\n");

		client.write('${level}|${StringTools.hex(color, 6)}|$safeMessage\n');
		client.output.flush();
	}
	#else
	public static function start(port:Int = 1145):Void {}
	public static function stop():Void {}
	public static function sendTraceMessage(level:String, message:String, color:Int):Void {}
	#end
}
