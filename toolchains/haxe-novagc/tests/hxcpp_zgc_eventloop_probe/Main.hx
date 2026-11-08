import sys.thread.Thread;

class Main {
    static function main():Void {
        Thread.current().events.progress();
        Thread.runWithEventLoop(() -> {});
    }
}
