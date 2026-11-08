set pagination off
set confirm off
set print thread-events off
run
echo \n===== CRASH BACKTRACE =====\n
bt 80
echo \n===== CRASH FRAME =====\n
frame 0
info args
info locals
p/x this
p/x graph
p/x graph.mPtr
echo \n===== ALL THREADS =====\n
thread apply all bt 24
quit
