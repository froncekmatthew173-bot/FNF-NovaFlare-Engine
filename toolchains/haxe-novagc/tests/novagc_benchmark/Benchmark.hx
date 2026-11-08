@:keep
private extern class NativeClock {
	@:native("__hxcpp_time_stamp") static function stamp():Float;
}

private class BenchNode {
	public var next:BenchNode;
	public var a:Int;
	public var b:Float;
	public var c:Int;
	public var d:Float;

	public function new(next:BenchNode, value:Int) {
		this.next = next;
		a = value;
		b = value * 0.5;
		c = value ^ 0x5a5a5a5a;
		d = value * 0.25;
	}
}

class Benchmark {
	static var root0:BenchNode;
	static var root1:BenchNode;
	static var root2:BenchNode;
	static var root3:BenchNode;
	static var root4:BenchNode;
	static var root5:BenchNode;
	static var root6:BenchNode;
	static var root7:BenchNode;

	static inline var WARMUP_FRAMES = 200;
	static inline var TEST_FRAMES = 1600;
	static inline var ALLOCS_PER_FRAME = 1800;

	static function setRoot(index:Int, value:BenchNode):Void {
		switch (index & 7) {
			case 0: root0 = value;
			case 1: root1 = value;
			case 2: root2 = value;
			case 3: root3 = value;
			case 4: root4 = value;
			case 5: root5 = value;
			case 6: root6 = value;
			default: root7 = value;
		}
	}

	static function runFrames(count:Int, record:Bool):Array<Float> {
		var samples:Array<Float> = [];
		var checksum = 0;
		for (frame in 0...count) {
			var started = NativeClock.stamp();
			var chain:BenchNode = null;
			for (i in 0...ALLOCS_PER_FRAME)
				chain = new BenchNode(chain, frame * ALLOCS_PER_FRAME + i);
			setRoot(frame, chain);
			checksum ^= chain.a;
			if (record) samples.push((NativeClock.stamp() - started) * 1000.0);
		}
		if (checksum == 0x7fffffff) Sys.println("unreachable");
		return samples;
	}

	static function percentile(sorted:Array<Float>, fraction:Float):Float
		return sorted[Std.int((sorted.length - 1) * fraction)];

	static function main():Void {
		runFrames(WARMUP_FRAMES, false);
		var samples = runFrames(TEST_FRAMES, true);
		samples.sort((a, b) -> a < b ? -1 : a > b ? 1 : 0);
		var sum = 0.0;
		for (sample in samples) sum += sample;
		untyped __cpp__("::hx::novagc::Runtime::collect()");
		untyped __cpp__("::hx::novagc::Runtime::startConcurrentOldCollection(4)");
		untyped __cpp__("::hx::novagc::Runtime::finishConcurrentOldCollection()");
		Sys.println('frames=${samples.length}');
		Sys.println('mean_ms=${sum / samples.length}');
		Sys.println('p95_ms=${percentile(samples, 0.95)}');
		Sys.println('p99_ms=${percentile(samples, 0.99)}');
		Sys.println('max_ms=${samples[samples.length - 1]}');
		untyped __cpp__("{ const ::hx::novagc::SchedulerStats s = ::hx::novagc::Runtime::schedulerStats(); fprintf(stdout, \"nursery_mb=%.3f\\npause_p99_ms=%.6f\\ndebt_ms=%.6f\\n\", double(s.nurseryTargetBytes)/1048576.0, s.pauseP99Millis, s.debtMillis); }");
	}
}
