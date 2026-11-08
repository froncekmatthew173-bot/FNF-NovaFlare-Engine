set pagination off
set confirm off
set print thread-events off
set breakpoint pending on
set environment HXCPP_NOVAGC_TELEMETRY 1
set logging file D:/game/FNF-NovaFlare-Engine/_build/gdb-first-exception-monitor.log
set logging overwrite on
set logging redirect off
set logging enabled on
break D:/game/FNF-NovaFlare-Engine/export/release/windows/obj/src/scripts/lua/CallbackHandler.cpp:148
commands 1
  silent
  set $lua_type_1 = lua_type(l, 1)
  set $lua_type_2 = lua_type(l, 2)
  if nparams == 2 && $lua_type_2 == 3
    set $lua_text_1 = (char*)lua_tolstring(l, 1, 0)
    set $lua_number_2 = lua_tonumber(l, 2)
    printf "\n===== LUA CALLBACK SOURCE ARGUMENTS =====\n"
    printf "nparams=%d lua_type_1=%d lua_text_1=%s lua_type_2=%d lua_number_2=%.17g\n", nparams, $lua_type_1, $lua_text_1, $lua_type_2, $lua_number_2
    bt 4
    printf "===== END LUA CALLBACK SOURCE ARGUMENTS =====\n"
  end
  continue
end
break D:/game/FNF-NovaFlare-Engine/export/release/windows/obj/src/scripts/lua/ReflectionFunctions.cpp:185
commands 2
  silent
  printf "\n===== SETPROPERTY CLOSURE ARGUMENTS =====\n"
  printf "arg0 tag=%d scalar=%lld ref=%llx\n", _hx_0.tag_, _hx_0.integer_, _hx_0.mPtr.storage
  printf "arg1 tag=%d scalar=%lld ref=%llx\n", _hx_1.tag_, _hx_1.integer_, _hx_1.mPtr.storage
  printf "arg2 tag=%d scalar=%lld ref=%llx\n", _hx_2.tag_, _hx_2.integer_, _hx_2.mPtr.storage
  bt 8
  printf "===== END SETPROPERTY CLOSURE ARGUMENTS =====\n"
  continue
end
break D:/game/FNF-NovaFlare-Engine/hxcpp/runtime/core/dynamic.cpp:597
commands 3
  silent
  printf "\n===== UNKNOWN DYNAMIC FIELD =====\n"
  printf "name=%s access=%d object=%p\n", fieldName, access, object
  bt 60
  printf "===== END UNKNOWN DYNAMIC FIELD =====\n"
  continue
end
catch throw
commands 4
  silent
  printf "\n===== FIRST-CHANCE C++ EXCEPTION =====\n"
  bt 100
  printf "===== END FIRST-CHANCE EXCEPTION =====\n"
  continue
end
handle SIGSEGV stop print nopass
run
printf "\n===== PROCESS STOP/CRASH =====\n"
bt 120
printf "\n===== ALL THREADS =====\n"
thread apply all bt 30
set logging enabled off
quit
