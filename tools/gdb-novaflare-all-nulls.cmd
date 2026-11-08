set pagination off
set confirm off
set print thread-events off
set breakpoint pending on
set environment HXCPP_NOVAGC_TELEMETRY 1
set environment HXCPP_NOVAGC_VERIFY_REFERENCES 1
set environment HXCPP_NOVAGC_VERIFY_REMEMBERED 1
set $nova_null_hits = 0
break hx::throwNullObjectReference()
commands 1
  silent
  set $nova_null_hits = $nova_null_hits + 1
  printf "\n===== HAXE NULL OBJECT REFERENCE hit %d =====\n", $nova_null_hits
  bt 100
  printf "===== END HAXE NULL hit %d =====\n", $nova_null_hits
  continue
end
run
