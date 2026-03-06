fn main() {
    let n: i64 = 200000;
    let mut acc: i64 = 0;
    let mut i: i64 = 0;
    while i < n {
        acc += i * i;
        i += 1;
    }
    println!("{}", acc);
}
