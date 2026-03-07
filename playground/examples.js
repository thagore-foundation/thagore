export const EXAMPLES = [
  {
    id: "intent",
    name: "Intent Sort",
    tag: "intent",
    color: "var(--lime)",
    code: `# Intent demo — browser playground uses the lowered Thagore version.
# In the native compiler this can be expressed with an intent block.

func sort3(a: i32, b: i32, c: i32):
  let x = a
  let y = b
  let z = c
  let tmp = 0

  if (x > y):
    tmp = x
    x = y
    y = tmp

  if (y > z):
    tmp = y
    y = z
    z = tmp

  if (x > y):
    tmp = x
    x = y
    y = tmp

  println("sorted")
  println(from_int(x))
  println(from_int(y))
  println(from_int(z))

func main():
  sort3(42, 7, 19)
`,
  },
  {
    id: "flow",
    name: "Flow + Saga",
    tag: "flow",
    color: "var(--amber)",
    code: `# Flow demo — staged control flow with explicit compensation points.

func checkout() -> str:
  println("stage: checkout")
  return "order-1"

func charge(order: str) -> str:
  println("stage: charge")
  println(order)
  return "payment-1"

func notify(payment: str):
  println("stage: notify")
  println(payment)

func main():
  let order = checkout()
  let payment = charge(order)
  notify(payment)
  println("Order confirmed!")
`,
  },
  {
    id: "types",
    name: "Type Inference",
    tag: "types",
    color: "var(--sky)",
    code: `# Type inference with plain Thagore syntax.

func dot(ax: f64, ay: f64, az: f64, bx: f64, by: f64, bz: f64) -> f64:
  return ax * bx + ay * by + az * bz

func main():
  let ax = 1.0
  let ay = 2.0
  let az = 3.0
  let bx = 4.0
  let by = 5.0
  let bz = 6.0

  let result = dot(ax, ay, az, bx, by, bz)
  println("dot =")
  println(from_f64(result))
`,
  },
  {
    id: "conc",
    name: "Concurrency",
    tag: "conc",
    color: "var(--pur)",
    code: `# Concurrency demo — browser playground runs the sequential fallback.

func sum_to(limit: i32) -> i32:
  let i = 0
  let total = 0
  while (i <= limit):
    total = total + i
    i = i + 1
  return total

func main():
  println("sum =")
  println(from_int(sum_to(1000)))
`,
  },
  {
    id: "ffi",
    name: "Native FFI",
    tag: "ffi",
    color: "var(--red)",
    code: `# Native FFI demo — browser playground shows the safe pure-Thagore path.

func main():
  let msg = "Hello from Thagore"
  println(msg)
  println(to_upper("ffi bridge"))
`,
  },
];
