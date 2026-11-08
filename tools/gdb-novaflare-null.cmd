set pagination off
set confirm off
set print thread-events off
set breakpoint pending on
set environment HXCPP_NOVAGC_TELEMETRY 1
break hx::throwNullObjectReference()
commands 1
  silent
  echo \n===== FIRST HAXE NULL OBJECT REFERENCE =====\n
  bt 100
  echo \n===== ALL THREADS AT FIRST NULL =====\n
  thread apply all bt 20
  quit
end
run
