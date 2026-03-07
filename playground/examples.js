export const EXAMPLES = [
  {
    name: "Hello, World!",
    code: `func main():
  println("Hello, World!")
`,
  },
  {
    name: "Fibonacci",
    code: `func fib(n: i32) -> i32:
  if (n <= 1):
    return n
  return fib(n - 1) + fib(n - 2)

func main():
  let i = 0
  while (i < 10):
    println(from_int(fib(i)))
    i = i + 1
`,
  },
  {
    name: "FizzBuzz",
    code: `func main():
  let i = 1
  while (i <= 30):
    if (i % 15 == 0):
      println("FizzBuzz")
    else:
      if (i % 3 == 0):
        println("Fizz")
      else:
        if (i % 5 == 0):
          println("Buzz")
        else:
          println(from_int(i))
    i = i + 1
`,
  },
  {
    name: "Math",
    code: `func main():
  println(from_f64(sqrt(2.0)))
  println(from_f64(pow(3.0, 4.0)))
  println(from_int(gcd(48, 18)))
`,
  },
  {
    name: "String operations",
    code: `func main():
  let s = "Hello, Thagore!"
  println(to_upper(s))
  println(from_int(len(s)))
  println(replace(s, "Thagore", "Playground"))
`,
  },
  {
    name: "Conditionals",
    code: `func sign(x: i32) -> str:
  if (x < 0):
    return "negative"
  else:
    if (x == 0):
      return "zero"
    return "positive"

func main():
  println(sign(-7))
  println(sign(0))
  println(sign(12))
`,
  },
  {
    name: "Bubble Sort",
    code: `func swap_if_needed(a: i32, b: i32) -> i32:
  if (a > b):
    return 1
  return 0

func main():
  let a = 5
  let b = 2
  let c = 4
  let tmp = 0

  if (swap_if_needed(a, b) == 1):
    tmp = a
    a = b
    b = tmp

  if (swap_if_needed(b, c) == 1):
    tmp = b
    b = c
    c = tmp

  if (swap_if_needed(a, b) == 1):
    tmp = a
    a = b
    b = tmp

  println(from_int(a))
  println(from_int(b))
  println(from_int(c))
`,
  },
  {
    name: "Binary Search",
    code: `func value_at(index: i32) -> i32:
  if (index == 0):
    return 3
  else:
    if (index == 1):
      return 8
    else:
      if (index == 2):
        return 12
      else:
        if (index == 3):
          return 19
        return 25

func binary_search(target: i32) -> i32:
  let left = 0
  let right = 4
  while (left <= right):
    let mid = (left + right) / 2
    let value = value_at(mid)
    if (value == target):
      return mid
    else:
      if (value < target):
        left = mid + 1
      else:
        right = mid - 1
  return -1

func main():
  println(from_int(binary_search(19)))
  println(from_int(binary_search(7)))
`,
  },
];
