declare i32 @puts(ptr)
@.str0 = private unnamed_addr constant [25 x i8] c"Hello Self-Hosted World!\00"
define i32 @main() {
entry:
  call i32 @puts(ptr @.str0)
  ret i32 0
}
